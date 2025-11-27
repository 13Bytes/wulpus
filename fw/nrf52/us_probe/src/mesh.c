#include "mesh.h"
#include "ble.h"
#include "helper.h"
#include "main.h"
#include "spi.h"
#include <bluetooth/mesh/models.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mesh);

// --- Globals ---
struct bt_mesh_model *vnd_model;
K_MSGQ_DEFINE(mesh_tx_msgq, sizeof(frame_chunk), MESH_TX_QUEUE_SIZE, 4);
K_MUTEX_DEFINE(mesh_pub_mutex);

static uint8_t reassembly_buffer[BLE_PCKT_SEND_SIZE];

// --- Functions ---
static int output_number(bt_mesh_output_action_t action, uint32_t number) {
  LOG_INF("OOB Number: %u\n", number);
  return 0;
}

static void prov_complete(uint16_t net_idx, uint16_t addr) {
  LOG_INF("Provisioning completed with net_idx: 0x%04x, addr: 0x%04x", net_idx,
          addr);
}

static void prov_reset(void) {
  bt_mesh_prov_enable(
      (bt_mesh_prov_bearer_t)(BT_MESH_PROV_GATT | BT_MESH_PROV_ADV));
  LOG_WRN("The local node has been reset and needs reprovisioning");
}

uint8_t dev_uuid[16] = {0}; // Will be filled in main

const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .output_size = 4,
    .output_actions = BT_MESH_DISPLAY_NUMBER,
    .output_number = output_number,
    .complete = prov_complete,
    .reset = prov_reset,
};

/* Health Server */
static void attention_on(const struct bt_mesh_model *mod) {
  LOG_INF("Attention ON");
}

static void attention_off(const struct bt_mesh_model *mod) {
  LOG_INF("Attention OFF");
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
    .attn_on = attention_on,
    .attn_off = attention_off,
};

static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

/* Data model (Custom Vendor Model)  */
BT_MESH_MODEL_PUB_DEFINE(vnd_model_pub, NULL, BT_MESH_TX_SDU_MAX);

static int mesh_receiving_start_config(const struct bt_mesh_model *model,
                                       struct bt_mesh_msg_ctx *ctx,
                                       struct net_buf_simple *buf) {
  if (address_is_local(bt_mesh_model_elem(model), ctx->addr)) {
    return 0;
  }
  LOG_INF("<-- RX Start Config <0x%04x>", ctx->addr);
  if (buf->len < sizeof(frame_chunk_header)) {
    return -EINVAL;
  }

  config_frame cfg = {0};
  size_t len = buf->len;
  memcpy(&cfg.data, buf->data, len);

  apply_config(&cfg, len);

  return 0;
}

int mesh_publish_config(const uint8_t *config_data, size_t len) {
  if (!config_data || len == 0 || len > BLE_SINGLE_PCKT_SIZE) {
    LOG_ERR("Invalid config data: len=%u", (unsigned)len);
    return -EINVAL;
  }

  if (!bt_mesh_is_provisioned()) {
    LOG_WRN("Cannot publish config: not provisioned");
    return -EAGAIN;
  }

  struct bt_mesh_model *mod = bt_mesh_model_find_vnd(
      comp.elem, BT_COMP_ID_LF, BT_MESH_VND_MODEL_ID_WULPUS);
  if (!mod || !mod->pub || !mod->pub->msg) {
    LOG_ERR("Vendor model or publication not configured");
    return -ENODEV;
  }

  if (mod->pub->addr == BT_MESH_ADDR_UNASSIGNED) {
    LOG_WRN("Model publication not configured. Using broadcast all (0xffff)");
    mod->pub->addr = BT_MESH_ADDR_ALL_NODES;
  }

  k_mutex_lock(&mesh_pub_mutex, K_FOREVER);

  bt_mesh_model_msg_init(mod->pub->msg, BT_MESH_VND_OP_WULPUS_START_CONFIG);
  net_buf_simple_add_mem(mod->pub->msg, config_data, len);
  int err = bt_mesh_model_publish(mod);
  if (err) {
    LOG_ERR("Failed to publish config: %d", err);
  } else {
    LOG_INF("--> TX Start Config (len=%u)", (unsigned)len);
  }
  k_mutex_unlock(&mesh_pub_mutex);
  return err;
}

// Handle received data in Mesh
// Assemble chunks and send them out over BLE
static int mesh_receiving_data_chunk(const struct bt_mesh_model *model,
                                     struct bt_mesh_msg_ctx *ctx,
                                     struct net_buf_simple *buf) {
  LOG_INF("<-- RX message <0x%04x>", ctx->addr);

  if (buf->len < sizeof(frame_chunk_header)) {
    LOG_WRN("Received chunk too short");
    return -EINVAL;
  }

  frame_chunk_header header;
  header.timestamp = net_buf_simple_pull_le32(buf);
  header.offset = net_buf_simple_pull_u8(buf);
  header.size = net_buf_simple_pull_u8(buf);

  // UPDATED LOGIC: Offset = Chunk Index, Size = Bytes
  size_t data_len = header.size;
  size_t byte_offset = header.offset * BYTES_PR_XFER_RX;

  if (buf->len < data_len) {
    LOG_WRN("Chunk data length mismatch");
    return -EINVAL;
  }

  if (byte_offset + data_len > sizeof(reassembly_buffer)) {
    LOG_WRN("Chunk out of bounds: off=%u, len=%u", (unsigned)byte_offset,
            (unsigned)data_len);
    return -EINVAL;
  }

  memcpy(&reassembly_buffer[byte_offset], buf->data, data_len);
  LOG_INF("RX Chunk: TS=%u, Off=%u (idx), Size=%u", header.timestamp,
          header.offset, header.size);

  // Forward to BLE if connected and this is the last chunk
  if (current_conn && header.offset == CHUNKS_PER_FRAME - 1) {
    ble_data_t tx_item;
    tx_item.len = BLE_PCKT_SEND_SIZE;
    memcpy(tx_item.data, reassembly_buffer, BLE_PCKT_SEND_SIZE);

    // Use K_NO_WAIT to avoid blocking Mesh thread
    if (k_msgq_put(&ble_tx_msgq, &tx_item, K_NO_WAIT) != 0) {
      LOG_WRN("BLE TX queue full, dropping forwarded mesh frame");
    }
  }

  return 0;
}

static const struct bt_mesh_model_op vnd_model_op[] = {
    {BT_MESH_VND_OP_WULPUS_FRAMECHUNK,
     BT_MESH_LEN_MIN(sizeof(frame_chunk_header)), mesh_receiving_data_chunk},
    {BT_MESH_VND_OP_WULPUS_START_CONFIG,
     BT_MESH_LEN_MIN(sizeof(frame_chunk_header)), mesh_receiving_start_config},
    BT_MESH_MODEL_OP_END,
};

static const struct bt_mesh_elem elements[] = {BT_MESH_ELEM(
    0,
    BT_MESH_MODEL_LIST(BT_MESH_MODEL_CFG_SRV,
                       BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub)),
    BT_MESH_MODEL_LIST(BT_MESH_MODEL_VND(BT_COMP_ID_LF,
                                         BT_MESH_VND_MODEL_ID_WULPUS,
                                         vnd_model_op, &vnd_model_pub, NULL)))};

const struct bt_mesh_comp comp = {
    .cid = BT_COMP_ID_LF,
    .elem_count = ARRAY_SIZE(elements),
    .elem = elements,
};

void mesh_tx_thread(void) {
  LOG_INF("Mesh TX thread spawned and waiting for data to send...");
  static struct frame_chunk tx_data; // static to reduce stack usage
  struct bt_mesh_model *mod = bt_mesh_model_find_vnd(
      comp.elem, BT_COMP_ID_LF, BT_MESH_VND_MODEL_ID_WULPUS);

  if (!mod) {
    LOG_ERR("Vendor model not found");
    return;
  } else if (!mod->pub || !mod->pub->msg) {
    LOG_ERR("Model has no pub defined");
    return;
  }
  if (mod->pub->addr == BT_MESH_ADDR_UNASSIGNED) {
    LOG_WRN("Model publication not configured. Using broadcast all (0xffff)");
    mod->pub->addr = BT_MESH_ADDR_ALL_NODES;
  }

  while (1) {
    k_msgq_get(&mesh_tx_msgq, &tx_data, K_FOREVER);
    LOG_INF("Mesh TX thread got frame");

    if (!bt_mesh_is_provisioned()) {
      continue;
    }

    uint32_t timestamp = k_ticks_to_us_floor32(k_uptime_ticks());
    size_t total_sent = 0;
    uint16_t block_idx =
        0; // must be dividable by 2, as the number send is for each two blocks

    LOG_INF("--> TX message (start)");

    // Check if we are trying to send segmented data to a broadcast/group
    // address Segmented messages (required for len > 11) are NOT allowed to
    // group addresses
    if (BT_MESH_ADDR_IS_GROUP(mod->pub->addr) ||
        mod->pub->addr == BT_MESH_ADDR_ALL_NODES) {
      LOG_WRN("Cannot send large frame (segmented) to Broadcast/Group address "
              "(0x%04x).",
              mod->pub->addr);
      LOG_WRN("Please provision the device and configure publication to a "
              "Unicast address.");
      continue; // Skip this frame
    }

    k_mutex_lock(&mesh_pub_mutex, K_FOREVER);
    while (total_sent < BLE_PCKT_SEND_SIZE) {
      bt_mesh_model_msg_init(mod->pub->msg, BT_MESH_VND_OP_WULPUS_FRAMECHUNK);
      size_t remaining_bytes = BLE_PCKT_SEND_SIZE - total_sent;
      // Max payload of BLE is ~370 bytes; Header is 6 bytes
      // We use 16-byte blocks. 364 * 8 / 16 ≈ 180 blocks max (360 bytes)
      size_t blocks_to_send = remaining_bytes / 2;
      blocks_to_send = MIN(blocks_to_send, 180);
      if (blocks_to_send == 0 && remaining_bytes > 0) {
        LOG_ERR("Remaining data less than 16 bytes - This shouldn't happen");
        blocks_to_send = 1;
      }
      frame_chunk_header header = {
          .timestamp = tx_data.header.timestamp,
          .offset = (uint8_t)(block_idx / 2),
          .size = blocks_to_send,
      };
      size_t bytes_to_send = blocks_to_send * 2;

      net_buf_simple_add_le32(mod->pub->msg, header.timestamp);
      net_buf_simple_add_u8(mod->pub->msg, header.offset);
      net_buf_simple_add_u8(mod->pub->msg, header.size);
      net_buf_simple_add_mem(mod->pub->msg, &tx_data.data[total_sent],
                             bytes_to_send);

      int err = bt_mesh_model_publish(mod);
      if (err) {
        LOG_WRN("Mesh publish failed at block %d: %d", block_idx, err);
        // If we failed, we should probably retry or abort, but for now let's
        // just wait a bit
        k_sleep(K_MSEC(10));
      } else {
        // Only increment if successful (or if we want to skip failed chunks)
        // For now, let's assume we move on to avoid getting stuck
        total_sent += bytes_to_send;
        block_idx += blocks_to_send;
      }

      // Yield to let the stack process
      k_yield();
    }
    k_mutex_unlock(&mesh_pub_mutex);
  }
}
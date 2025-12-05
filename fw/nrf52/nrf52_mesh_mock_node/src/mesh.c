#include "mesh.h"
#include <bluetooth/mesh/models.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>

LOG_MODULE_REGISTER(mesh);

// --- Globals ---
static struct bt_mesh_model *mod;
static bool sending_enabled = false;
static uint16_t gateway_addr = BT_MESH_ADDR_UNASSIGNED;
static struct frame_chunk tx_data;
K_SEM_DEFINE(mesh_send_sem, 0, 1);

// --- Forward Declarations ---
static int mesh_receiving_data_chunk(const struct bt_mesh_model *model,
                                     struct bt_mesh_msg_ctx *ctx,
                                     struct net_buf_simple *buf);
static int mesh_receiving_start_config(const struct bt_mesh_model *model,
                                       struct bt_mesh_msg_ctx *ctx,
                                       struct net_buf_simple *buf);
static int mesh_receiving_gateway_update(const struct bt_mesh_model *model,
                                         struct bt_mesh_msg_ctx *ctx,
                                         struct net_buf_simple *buf);
static int mesh_receiving_gateway_req(const struct bt_mesh_model *model,
                                      struct bt_mesh_msg_ctx *ctx,
                                      struct net_buf_simple *buf);

// --- Models ---
static struct bt_mesh_cfg_cli cfg_cli;

/* Health Server */
static void attention_on(const struct bt_mesh_model *mod)
{
  LOG_INF("Attention ON");
}
static void attention_off(const struct bt_mesh_model *mod)
{
  LOG_INF("Attention OFF");
}
static const struct bt_mesh_health_srv_cb health_srv_cb = {
    .attn_on = attention_on,
    .attn_off = attention_off,
};
static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

static const struct bt_mesh_model_op vnd_model_op[] = {
    {BT_MESH_VND_OP_WULPUS_FRAMECHUNK, BT_MESH_LEN_MIN(sizeof(frame_chunk_header)), mesh_receiving_data_chunk},
    {BT_MESH_VND_OP_WULPUS_START_CONFIG, BT_MESH_LEN_MIN(6), mesh_receiving_start_config},
    {BT_MESH_VND_OP_WULPUS_GATEWAY_UPDATE, BT_MESH_LEN_MIN(2), mesh_receiving_gateway_update},
    {BT_MESH_VND_OP_WULPUS_GATEWAY_REQ, BT_MESH_LEN_MIN(1), mesh_receiving_gateway_req},
    BT_MESH_MODEL_OP_END,
};

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);
/* Data model (Custom Vendor Model)  */
BT_MESH_MODEL_PUB_DEFINE(vnd_model_pub, NULL, BT_MESH_TX_SDU_MAX);

static const struct bt_mesh_elem elements[] = {BT_MESH_ELEM(
    0,
    BT_MESH_MODEL_LIST(BT_MESH_MODEL_CFG_SRV, BT_MESH_MODEL_CFG_CLI(&cfg_cli),
                       BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub)),
    BT_MESH_MODEL_LIST(BT_MESH_MODEL_VND(BT_MESH_VND_ID,
                                         BT_MESH_VND_MODEL_ID_WULPUS,
                                         vnd_model_op, &vnd_model_pub, NULL)))};

static const struct bt_mesh_comp comp = {
    .cid = BT_MESH_VND_ID,
    .elem_count = ARRAY_SIZE(elements),
    .elem = elements,
};

// --- Implementation ---

static int mesh_receiving_start_config(const struct bt_mesh_model *model,
                                       struct bt_mesh_msg_ctx *ctx,
                                       struct net_buf_simple *buf)
{
  LOG_INF("RX Start Config from 0x%04x", ctx->addr);
  if (buf->len > 0 && buf->data[0] != 0x00)
  {
    LOG_INF("Config received. Starting data transmission.");
    sending_enabled = true;
  }
  else
  {
    LOG_INF("Empty config received. Stopping data transmission.");
    sending_enabled = false;
  }
  return 0;
}

static int mesh_receiving_gateway_update(const struct bt_mesh_model *model,
                                         struct bt_mesh_msg_ctx *ctx,
                                         struct net_buf_simple *buf)
{
  if (buf->len < 2)
    return -EINVAL;
  uint16_t new_addr = net_buf_simple_pull_le16(buf);
  LOG_INF("Gateway address updated to 0x%04x", new_addr);
  gateway_addr = new_addr;

  // Update publication address
  if (mod && mod->pub)
  {
    mod->pub->addr = gateway_addr;
  }
  return 0;
}

static int mesh_receiving_gateway_req(const struct bt_mesh_model *model,
                                      struct bt_mesh_msg_ctx *ctx,
                                      struct net_buf_simple *buf)
{
  LOG_INF("Received Gateway Request from 0x%04x", ctx->addr);
  // We are a node, we currenlty don't do anything
  // TODO: share if we already know it
  return 0;
}

void mesh_request_gateway_addr(void)
{
  if (!mod || !mod->pub)
    return;

  struct net_buf_simple *msg = mod->pub->msg;
  uint16_t my_addr = bt_mesh_primary_addr();
  bt_mesh_model_msg_init(msg, BT_MESH_VND_OP_WULPUS_GATEWAY_REQ);
  net_buf_simple_add_mem(msg, &my_addr, sizeof(my_addr));

  // Send to group
  struct bt_mesh_msg_ctx ctx = {
      .addr = WULPUS_GROUP_ADDR,
      .app_idx = mod->pub->key,
      .send_ttl = BT_MESH_TTL_DEFAULT,
  };

  if (bt_mesh_model_send(mod, &ctx, msg, NULL, NULL))
  {
    LOG_ERR("Failed to send gateway request");
  }
  else
  {
    LOG_INF("Sent Gateway Request");
  }
}

static void mesh_send_end(int err, void *cb_data)
{
  k_sem_give(&mesh_send_sem);
}
static const struct bt_mesh_send_cb send_cb = {
    .end = mesh_send_end,
};

// --- TX Thread ---
static void mesh_tx_thread_entry(void *p1, void *p2, void *p3)
{
  LOG_INF("Mesh TX thread started");

  // Wait for provisioning
  while (!bt_mesh_is_provisioned())
  {
    k_sleep(K_SECONDS(1));
  }

  // Find our model
  mod = bt_mesh_model_find_vnd(comp.elem, BT_MESH_VND_ID,
                               BT_MESH_VND_MODEL_ID_WULPUS);

  // Request gateway
  mesh_request_gateway_addr();

  while (1)
  {
    if (sending_enabled && gateway_addr != BT_MESH_ADDR_UNASSIGNED &&
        mod && mod->pub)
    {

      size_t total_sent = 0;
      uint16_t block_idx = 0; // must be dividable by 2, as the number send is for each two blocks
      tx_data.header.timestamp = k_uptime_get_32();
      uint8_t random_sample_data = tx_data.header.timestamp & 0xFF;
      for (unsigned i = 0; i < BLE_PCKT_SEND_SIZE; i++)
      {
        tx_data.data[i] = random_sample_data;
      }

      LOG_INF("--> TX message (start)");
      while (total_sent < BLE_PCKT_SEND_SIZE)
      {
        bt_mesh_model_msg_init(mod->pub->msg, BT_MESH_VND_OP_WULPUS_FRAMECHUNK);
        size_t remaining_bytes = BLE_PCKT_SEND_SIZE - total_sent;
        // Max payload of BLE is ~370 bytes; Header is 6 bytes
        // We use 16-byte blocks. 364 * 8 / 16 ≈ 180 blocks max (360 bytes)
        size_t blocks_to_send = remaining_bytes / 2;
        blocks_to_send = MIN(blocks_to_send, 180);
        if (blocks_to_send == 0 && remaining_bytes > 0)
        {
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
        mod->pub->ttl = CONFIG_BT_MESH_DEFAULT_TTL;

        struct bt_mesh_msg_ctx ctx = {
            .addr = mod->pub->addr,
            .app_idx = mod->pub->key,
            .send_ttl = CONFIG_BT_MESH_DEFAULT_TTL,
        };

        LOG_INF("-->     block %d (%d)", block_idx, header.timestamp);
        int err = bt_mesh_model_send(mod, &ctx, mod->pub->msg, &send_cb, NULL);
        if (err)
        {
          LOG_WRN("Sending failed at block %d: %d", block_idx, err);
          break;
        }
        else
        {
          k_sem_take(&mesh_send_sem, K_FOREVER);
          LOG_INF("        block %d (%d) successfully sent", block_idx,
                  header.timestamp);
          total_sent += bytes_to_send;
          block_idx += blocks_to_send;
        }

        // Yield to let the stack process
        k_yield();
      }

      k_sleep(K_MSEC(2000));
    }
    else
    {
      if (sending_enabled && gateway_addr == BT_MESH_ADDR_UNASSIGNED)
      {
        LOG_WRN("Want to send, but no gateway. Retrying request...");
        mesh_request_gateway_addr();
      }
      else if (gateway_addr == BT_MESH_ADDR_UNASSIGNED)
      {
        mesh_request_gateway_addr();
      }
      else
      {
        LOG_INF("nothing to do, sleeping...");
      }
      k_sleep(K_SECONDS(5));
    }
  }
}

K_THREAD_DEFINE(mesh_tx_thread, 2048, mesh_tx_thread_entry, NULL, NULL, NULL, 7,
                0, 0);

// --- Initialization ---
static void prov_complete(uint16_t net_idx, uint16_t addr)
{
  LOG_INF("Provisioning completed. Addr: 0x%04x", addr);
  mesh_request_gateway_addr();
}

static void prov_reset(void)
{
  bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
}

static uint8_t dev_uuid[16] = {0xdd, 0xdd}; // Default

static const uint16_t net_idx = 0;
static const uint16_t app_idx = 0;
static const uint32_t iv_index = 0;
static uint8_t flags = 0;

static const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .complete = prov_complete,
    .reset = prov_reset,
};

void mesh_init(void)
{
  int err = bt_mesh_init(&prov, &comp);
  if (err)
  {
    LOG_ERR("Initializing mesh failed (err %d)", err);
    return;
  }

  // Load settings if enabled (simplified: just enable provisioning)
  bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);

  LOG_INF("Mesh initialized");
}

int mesh_provision_self(uint16_t addr)
{
  int err;
  LOG_INF("Self-provisioning with address 0x%04x", addr);

  err = bt_mesh_provision(net_key, net_idx, flags, iv_index, addr, dev_key);
  if (err == -EALREADY)
  {
    LOG_WRN("Using stored settings");
  }
  else if (err)
  {
    LOG_ERR("Provisioning failed (err %d)", err);
    return err;
  }
  else
  {
    LOG_INF("Provisioning completed");
  }

  uint8_t bind_status;

  /* Add Application Key */
  err = bt_mesh_cfg_cli_app_key_add(net_idx, addr, net_idx, app_idx, app_key,
                                    &bind_status);
  if (err)
  {
    LOG_ERR("Failed to add AppKey (err %d)", err);
  }
  else
  {
    LOG_INF("AppKey added");
  }

  /* Bind to vendor model */
  err = bt_mesh_cfg_cli_mod_app_bind_vnd(net_idx, addr, addr, app_idx,
                                         BT_MESH_VND_MODEL_ID_WULPUS,
                                         BT_MESH_VND_ID, &bind_status);
  if (err)
  {
    LOG_ERR("Failed to bind Vendor Model (err %d)", err);
  }
  else
  {
    LOG_INF("Vendor Model bound");
  }

  /* Subscribe to Wulpus Group Address */
  err = bt_mesh_cfg_cli_mod_sub_add_vnd(net_idx, addr, addr, WULPUS_GROUP_ADDR,
                                        BT_MESH_VND_MODEL_ID_WULPUS,
                                        BT_MESH_VND_ID, &bind_status);
  if (err)
  {
    LOG_ERR("Failed to subscribe to Group Address (err %d)", err);
  }
  else
  {
    LOG_INF("Subscribed to Group Address 0x%04x", WULPUS_GROUP_ADDR);
  }

  return 0;
}

// Handle received data in Mesh
// Assemble chunks and send them out over BLE
static int mesh_receiving_data_chunk(const struct bt_mesh_model *model,
                                     struct bt_mesh_msg_ctx *ctx,
                                     struct net_buf_simple *buf)
{
  LOG_INF("<-- RX message <0x%04x>", ctx->addr);

  if (buf->len < sizeof(frame_chunk_header))
  {
    LOG_WRN("Received chunk too short");
    return -EINVAL;
  }

  frame_chunk_header header;
  header.timestamp = net_buf_simple_pull_le32(buf);
  header.offset = net_buf_simple_pull_u8(buf);
  header.size = net_buf_simple_pull_u8(buf);

  // UPDATED LOGIC: Offset = Chunk Index, Size = Bytes
  size_t data_len = header.size;
  size_t byte_offset = header.offset * 2;

  if (buf->len < data_len)
  {
    LOG_WRN("Chunk data length mismatch");
    return -EINVAL;
  }

  return 0;
}
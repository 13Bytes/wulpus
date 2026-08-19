#include "mesh.h"
#include "ble.h"
#include "helper.h"
#include "main.h"
#include "spi.h"
#include "timehelper.h"
#include "tx_stats.h"
#include <bluetooth/mesh/models.h>
#include <bluetooth/mesh/time_cli.h>
#include <bluetooth/mesh/time_srv.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/bluetooth/mesh/access.h>
#include <zephyr/bluetooth/mesh/main.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(mesh);

// --- Globals ---
struct bt_mesh_model *vnd_model;
K_MSGQ_DEFINE(mesh_tx_msgq, sizeof(frame_chunk), MESH_TX_QUEUE_SIZE, 4);
K_MUTEX_DEFINE(mesh_pub_mutex);
K_SEM_DEFINE(mesh_send_sem, 0, 1);
bool i_am_gateway = false;

static atomic_t mesh_last_send_err;

static void mesh_send_end(int err, void *cb_data) {
  atomic_set(&mesh_last_send_err, err);
  k_sem_give(&mesh_send_sem);
}

static const struct bt_mesh_send_cb send_cb = {
    .end = mesh_send_end,
};

static uint8_t reassembly_buffer[BLE_PCKT_SEND_SIZE];

// --- Time Models ------------------------------
static struct bt_mesh_time_srv time_srv = BT_MESH_TIME_SRV_INIT(NULL);
static struct bt_mesh_time_cli time_cli = BT_MESH_TIME_CLI_INIT(NULL);

// --- Time Sync --------------------------------
static void mesh_time_sync_thread(void *a, void *b, void *c);
K_THREAD_DEFINE(mesh_time_sync_thread_id, 2048, mesh_time_sync_thread, NULL,
                NULL, NULL, 7, 0, 0);

static uint16_t mesh_get_gateway_addr(void)
{
  const struct bt_mesh_model *mod = bt_mesh_model_find_vnd(
      comp.elem, BT_MESH_VND_ID, BT_MESH_VND_MODEL_ID_WULPUS);
  if (!mod || !mod->pub)
  {
    return BT_MESH_ADDR_UNASSIGNED;
  }
  return mod->pub->addr;
}

static bool mesh_addr_is_valid_unicast(uint16_t addr)
{
  if (addr == BT_MESH_ADDR_UNASSIGNED || addr == BT_MESH_ADDR_ALL_NODES)
  {
    return false;
  }
  if (BT_MESH_ADDR_IS_GROUP(addr) || BT_MESH_ADDR_IS_VIRTUAL(addr))
  {
    return false;
  }
  return true;
}

static bool mesh_time_is_known(void)
{
  struct bt_mesh_time_tai tai;
  network_time_into_tai(&time_srv, k_uptime_get(), &tai);
  return !tai_is_unknown(&tai);
}

// --- Functions ---
uint16_t mesh_primary_addr_get(void) {
  return comp.elem[0].rt->addr;
}

static int output_number(bt_mesh_output_action_t action, uint32_t number) {
  LOG_INF("OOB Number: %u\n", number);
  return 0;
}

#if !defined(CONFIG_BT_MESH_PROV_OOB_API_LEGACY)
static int output_numeric(bt_mesh_output_action_t action, uint8_t *numeric,
                          size_t size) {
  uint32_t number = 0;
  for (size_t i = 0; i < MIN(size, sizeof(number)); i++) {
    number |= ((uint32_t)numeric[i]) << (8 * i);
  }

  return output_number(action, number);
}
#endif

static void prov_complete(uint16_t net_idx, uint16_t addr) {
  LOG_INF("Provisioning completed with net_idx: 0x%04x, addr: 0x%04x", net_idx,
          addr);

  // Give the mesh stack time to fully initialize
  k_sleep(K_MSEC(100));

  int err;
  uint8_t bind_status;
  const uint16_t app_idx = 0x0000;

  // Add Application Key
  extern const uint8_t app_key[16];
  err = bt_mesh_cfg_cli_app_key_add(net_idx, addr, net_idx, app_idx, app_key,
                                    &bind_status);
  if (err) {
    LOG_ERR("Failed to add AppKey (err %d)", err);
  } else {
    LOG_INF("AppKey added successfully");
  }

  // Bind to vendor model
  err = bt_mesh_cfg_cli_mod_app_bind_vnd(net_idx, addr, addr, app_idx,
                                         BT_MESH_VND_MODEL_ID_WULPUS,
                                         BT_MESH_VND_ID, &bind_status);
  if (err) {
    LOG_ERR("Failed to bind Vendor Model (err %d)", err);
  } else {
    LOG_INF("Vendor Model bound successfully");
  }

  // Subscribe to Wulpus Group Address
  err = bt_mesh_cfg_cli_mod_sub_add_vnd(net_idx, addr, addr, WULPUS_GROUP_ADDR,
                                        BT_MESH_VND_MODEL_ID_WULPUS,
                                        BT_MESH_VND_ID, &bind_status);
  if (err) {
    LOG_ERR("Failed to subscribe to Group Address (err %d)", err);
  } else {
    LOG_INF("Subscribed to Group Address 0x%04x", WULPUS_GROUP_ADDR);
  }

  // Bind to Health model
  err = bt_mesh_cfg_cli_mod_app_bind(net_idx, addr, addr, app_idx,
                                     BT_MESH_MODEL_ID_HEALTH_SRV, &bind_status);
  if (err) {
    LOG_ERR("Failed to bind Health Model (err %d)", err);
  } else {
    LOG_INF("Health Model bound successfully");
  }

  // Bind to Time Server model (Element 1)
  err = bt_mesh_cfg_cli_mod_app_bind(net_idx, addr, addr + 1, app_idx,
                                     BT_MESH_MODEL_ID_TIME_SRV, &bind_status);
  if (err) {
    LOG_ERR("Failed to bind Time Server Model (err %d)", err);
  } else {
    LOG_INF("Time Server Model bound successfully");
  }

  // Bind to Time Client model (Element 2)
  err = bt_mesh_cfg_cli_mod_app_bind(net_idx, addr, addr + 2, app_idx,
                                     BT_MESH_MODEL_ID_TIME_CLI, &bind_status);
  if (err) {
    LOG_ERR("Failed to bind Time Client Model (err %d)", err);
  } else {
    LOG_INF("Time Client Model bound successfully");
  }

  mesh_request_gateway_addr();
}

static void prov_reset(void) {
  bt_mesh_prov_enable(
      (bt_mesh_prov_bearer_t)(BT_MESH_PROV_GATT | BT_MESH_PROV_ADV));
  LOG_WRN("The local node has been reset and needs reprovisioning");
}

const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .output_size = 4,
    .output_actions = BT_MESH_DISPLAY_NUMBER,
#if defined(CONFIG_BT_MESH_PROV_OOB_API_LEGACY)
    .output_number = output_number,
#else
    .output_numeric = output_numeric,
#endif
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

void mesh_set_time_authority(void) {
  LOG_INF("Setting local Time Role to AUTHORITY");
  bt_mesh_time_srv_role_set(&time_srv, BT_MESH_TIME_AUTHORITY);

  struct bt_mesh_time_status status = {
      .tai =
          {
              .sec = k_uptime_get() / 1000,
              .subsec = 0, // this introduces an error, but we dont care as we
                           // only will use network time from now on
          },
      .uncertainty = 0,
      .tai_utc_delta = 0,
      .time_zone_offset = 0,
      .is_authority = true,
  };

  bt_mesh_time_srv_time_set(&time_srv, k_uptime_get(), &status);
}

void mesh_unset_time_authority(void) {
  LOG_INF("Setting local Time Role to RELAY");
  bt_mesh_time_srv_role_set(&time_srv, BT_MESH_TIME_RELAY);
}

static void mesh_time_sync_thread(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);

  // Wait for provisioning before attempting model communication
  while (!bt_mesh_is_provisioned())
  {
    k_sleep(K_SECONDS(1));
  }

  // Ensure we start in RELAY role unless we later become authority
  mesh_unset_time_authority();

  while (1)
  {
    if (!bt_mesh_is_provisioned())
    {
      k_sleep(K_SECONDS(1));
      continue;
    }

    if (i_am_gateway)
    {
      // As gateway, provide an authoritative time base.
      if (!mesh_time_is_known())
      {
        mesh_set_time_authority();
      }
      k_sleep(K_SECONDS(10));
      continue;
    }

    uint16_t gw_addr = mesh_get_gateway_addr();
    uint16_t gw_time_srv_addr = (uint16_t)(gw_addr + 1);
    if (!mesh_addr_is_valid_unicast(gw_addr) || !mesh_addr_is_valid_unicast(gw_time_srv_addr) ||
        gw_addr == mesh_primary_addr_get())
    {
      k_sleep(K_SECONDS(5));
      continue;
    }

    struct bt_mesh_msg_ctx ctx = {
        .addr = gw_time_srv_addr,
        .app_idx = 0x0000,
        .send_ttl = CONFIG_BT_MESH_DEFAULT_TTL,
    };

    struct bt_mesh_time_status rsp = {0};
    int err = bt_mesh_time_cli_time_get(&time_cli, &ctx, &rsp);
    if (err)
    {
      LOG_WRN("Time-get from 0x%04x failed: %d", gw_time_srv_addr, err);
      k_sleep(K_SECONDS(2));
      continue;
    }

    // Apply received network time to our local Time Server instance so
    // network_time_into_tai() can provide synchronized timestamps.
    bt_mesh_time_srv_time_set(&time_srv, k_uptime_get(), &rsp);
    LOG_INF("Time synced from 0x%04x (sec=%u subsec=%u)", gw_time_srv_addr,
            (unsigned)rsp.tai.sec, (unsigned)rsp.tai.subsec);

    // Keep a gentle refresh cadence.
    k_sleep(mesh_time_is_known() ? K_SECONDS(30) : K_SECONDS(2));
  }
}

uint32_t mesh_get_network_timestamp(void) {
  struct bt_mesh_time_tai tai;
  network_time_into_tai(&time_srv, k_uptime_get(), &tai);
  if (tai_is_unknown(&tai)) {
    return k_uptime_get_32();
  }
  return (uint32_t)(tai.sec * 1000) + (uint32_t)(tai.subsec * 1000 / 256);
}

static struct bt_mesh_cfg_cli cfg_cli;

BT_MESH_HEALTH_PUB_DEFINE(health_pub, 0);

/* Data model (Custom Vendor Model)  */
BT_MESH_MODEL_PUB_DEFINE(vnd_model_pub, NULL, BT_MESH_TX_SDU_MAX);

int mesh_publish_self_gateway() {
  uint16_t my_addr = mesh_primary_addr_get();
  i_am_gateway = true;
  mesh_set_time_authority();
  return mesh_send_gateway_addr(my_addr);
}

int mesh_send_gateway_addr(uint16_t addr) {
  LOG_INF("Publishing gateway-addr: 0x%04x", addr);
  uint16_t my_addr = mesh_primary_addr_get();
  const struct bt_mesh_model *mod = bt_mesh_model_find_vnd(
      comp.elem, BT_MESH_VND_ID, BT_MESH_VND_MODEL_ID_WULPUS);

  if (!mod || !mod->pub || !mod->pub->msg) {
    LOG_ERR("mesh_send_gateway_addr: model or publication not configured");
    return -ENODEV;
  }

  k_mutex_lock(&mesh_pub_mutex, K_FOREVER);
  bt_mesh_model_msg_init(mod->pub->msg, BT_MESH_VND_OP_WULPUS_GATEWAY_UPDATE);
  net_buf_simple_add_le16(mod->pub->msg, addr);

  mod->pub->addr = WULPUS_GROUP_ADDR;
  struct bt_mesh_msg_ctx ctx = {
      .addr = WULPUS_GROUP_ADDR,
      .app_idx = mod->pub->key,
      .send_ttl = CONFIG_BT_MESH_DEFAULT_TTL,
  };
  int err = bt_mesh_model_send(mod, &ctx, mod->pub->msg, NULL, NULL);

  // Set new address as gateway
  mod->pub->addr = addr;
  i_am_gateway = (my_addr == addr);
  LOG_INF("I am gateway: %s", i_am_gateway ? "YES" : "no");

  k_mutex_unlock(&mesh_pub_mutex);

  if (err) {
    LOG_ERR("Failed to send gateway update: %d", err);
  } else {
    LOG_INF("Publishing gateway-addr: 0x%04x succeeded", addr);
  }
  return err;
}

void mesh_request_gateway_addr(void) {
  const struct bt_mesh_model *mod = bt_mesh_model_find_vnd(
      comp.elem, BT_MESH_VND_ID, BT_MESH_VND_MODEL_ID_WULPUS);

  if (!mod || !mod->pub || !mod->pub->msg) {
    LOG_ERR("mesh_request_gateway_addr: model or publication not configured");
    return;
  }
  uint16_t my_addr = mesh_primary_addr_get();
  k_mutex_lock(&mesh_pub_mutex, K_FOREVER);
  bt_mesh_model_msg_init(mod->pub->msg, BT_MESH_VND_OP_WULPUS_GATEWAY_REQ);
  net_buf_simple_add_mem(mod->pub->msg, &my_addr, sizeof(my_addr));

  uint16_t old_addr = mod->pub->addr;
  mod->pub->addr = WULPUS_GROUP_ADDR;
  struct bt_mesh_msg_ctx ctx = {
      .addr = WULPUS_GROUP_ADDR,
      .app_idx = mod->pub->key,
      .send_ttl = CONFIG_BT_MESH_DEFAULT_TTL,
  };
  int err = bt_mesh_model_send(mod, &ctx, mod->pub->msg, NULL, NULL);
  if (err) {
    LOG_ERR("Failed to request gateway addr: %d", err);
  } else {
    LOG_INF("Sent Gateway Request");
  }

  mod->pub->addr = old_addr;
  k_mutex_unlock(&mesh_pub_mutex);

  // If I am currently not the gateway, adapt flags
  if (old_addr != my_addr) {
    i_am_gateway = false;
    mesh_unset_time_authority();
  }
}

static int mesh_receiving_gateway_update(const struct bt_mesh_model *model,
                                         struct bt_mesh_msg_ctx *ctx,
                                         struct net_buf_simple *buf) {
  if (own_message(model, ctx)) {
    return 0;
  }

  LOG_INF("Received Gateway Update (from node 0x%04x)", ctx->addr);
  if (buf->len < 2) {
    return -EINVAL;
  }
  uint16_t new_addr = net_buf_simple_pull_le16(buf);

  struct bt_mesh_model *mod = (struct bt_mesh_model *)model;
  if (current_conn != NULL)
  {
    LOG_WRN("Ignoring Gateway Update (0x%04x) due to active BLE connection", new_addr);
    return 0;
  }
  if (mod->pub) {
    mod->pub->addr = new_addr;
    LOG_INF("Gateway address updated to 0x%04x (from 0x%04x)", new_addr,
            ctx->addr);
    if (new_addr != mesh_primary_addr_get()) {
      i_am_gateway = false;
      mesh_unset_time_authority();
    } else {
      i_am_gateway = true;
      mesh_set_time_authority();
    }
    return 0;
  }
  return -EINVAL;
}

static int mesh_receiving_gateway_req(const struct bt_mesh_model *model,
                                      struct bt_mesh_msg_ctx *ctx,
                                      struct net_buf_simple *buf) {
  if (own_message(model, ctx)) {
    return 0;
  }
  LOG_INF("Received Gateway Request from 0x%04x", ctx->addr);

  uint16_t stored_addr = model->pub->addr;
  if (stored_addr != BT_MESH_ADDR_UNASSIGNED &&
      stored_addr != BT_MESH_ADDR_ALL_NODES &&
      stored_addr != WULPUS_GROUP_ADDR) {
    mesh_send_gateway_addr(stored_addr);
  }

  return 0;
}

static int mesh_receiving_start_config(const struct bt_mesh_model *model,
                                       struct bt_mesh_msg_ctx *ctx,
                                       struct net_buf_simple *buf) {
  if (own_message(model, ctx)) {
    return 0;
  }
  LOG_INF("<-- RX Start Config <0x%04x>", ctx->addr);
  if (buf->len < sizeof(frame_chunk_header)) {
    return -EINVAL;
  }

  config_frame cfg = {0};
  size_t len = buf->len;
  memcpy(&cfg.data, buf->data, len);

  apply_config(cfg.data, len);

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

  const struct bt_mesh_model *mod = bt_mesh_model_find_vnd(
      comp.elem, BT_MESH_VND_ID, BT_MESH_VND_MODEL_ID_WULPUS);
  if (!mod || !mod->pub || !mod->pub->msg) {
    LOG_ERR("Vendor model or publication not configured");
    return -ENODEV;
  }

  k_mutex_lock(&mesh_pub_mutex, K_FOREVER);
  uint16_t old_addr = mod->pub->addr;
  mod->pub->addr = BT_MESH_ADDR_ALL_NODES;

  bt_mesh_model_msg_init(mod->pub->msg, BT_MESH_VND_OP_WULPUS_START_CONFIG);
  net_buf_simple_add_mem(mod->pub->msg, config_data, len);

  LOG_INF("TX Start Config: addr=0x%04x, ttl=%d, key=%d, len=%u",
          mod->pub->addr, mod->pub->ttl, mod->pub->key, (unsigned)len);

  mod->pub->addr = WULPUS_GROUP_ADDR;
  struct bt_mesh_msg_ctx ctx = {
      .addr = BT_MESH_ADDR_ALL_NODES,
      .app_idx = mod->pub->key,
      .send_ttl = CONFIG_BT_MESH_DEFAULT_TTL,
  };
  int err = bt_mesh_model_send(mod, &ctx, mod->pub->msg, NULL, NULL);
  if (err) {
    LOG_ERR("Failed to publish config: %d", err);
  } else {
    LOG_INF("--> TX Start Config sent");
  }
  mod->pub->addr = old_addr;
  k_mutex_unlock(&mesh_pub_mutex);
  return err;
}

// Handle received data in Mesh
// Assemble chunks and lster send them out over BLE
static int mesh_receiving_data_chunk(const struct bt_mesh_model *model,
                                     struct bt_mesh_msg_ctx *ctx,
                                     struct net_buf_simple *buf) {
  if (own_message(model, ctx)) {
    // own message will directly be sent over to BLE; detour not necessary
    return 0;
  }

  LOG_INF("<-- RX message <0x%04x>", ctx->addr);

  if (buf->len < sizeof(frame_chunk_header)) {
    LOG_WRN("Received chunk too short");
    return -EINVAL;
  }

  frame_chunk_header header;
  header.timestamp = net_buf_simple_pull_le32(buf);
  header.offset = net_buf_simple_pull_u8(buf);
  header.size = net_buf_simple_pull_u8(buf);

  size_t data_len = header.size * 2;
  size_t byte_offset = header.offset * 4;

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
  LOG_INF("    RX Chunk: TS=%u, Off=%u (idx), Size=%u", header.timestamp,
          header.offset, header.size);

  // Forward to BLE if frame complete
  const size_t reassembly_size = byte_offset + data_len;
  if (reassembly_size >= (BLE_PCKT_SEND_SIZE - 4)) {
    LOG_INF("Reassembled full frame, forwarding to BLE");
    frame_chunk tx_item;
    tx_item.header.size = reassembly_size;
    tx_item.header.timestamp = header.timestamp;
    tx_item.header.addr = ctx->addr;

    memcpy(tx_item.data, reassembly_buffer, BLE_PCKT_SEND_SIZE);

    // Use K_NO_WAIT to avoid blocking Mesh thread
    if (k_msgq_put(&ble_tx_msgq, &tx_item, K_NO_WAIT) != 0) {
      LOG_WRN("BLE TX queue full, dropping forwarded mesh frame");
      tx_stats_ble_frame_dropped_queue_full();
    }
  }

  return 0;
}

void mesh_tx_thread(void) {
  LOG_INF("Mesh TX thread spawned and waiting for data to send...");
  static struct frame_chunk tx_data; // static to reduce stack usage
  const struct bt_mesh_model *mod = bt_mesh_model_find_vnd(
      comp.elem, BT_MESH_VND_ID, BT_MESH_VND_MODEL_ID_WULPUS);

  /* --- Timing / profiling (ms resolution, low overhead) --- */
  enum
  {
    MESH_TX_TIMING_LOG_EVERY_FRAMES = 10,
  };

  uint32_t timing_frames = 0;
  uint64_t sum_queue_wait_ms = 0;
  uint64_t sum_frame_total_ms = 0;
  uint64_t sum_block_prep_ms = 0;
  uint64_t sum_block_send_call_ms = 0;
  uint64_t sum_block_wait_end_ms = 0;
  uint32_t max_block_wait_end_ms = 0;
  uint32_t max_frame_total_ms = 0;
  uint32_t timing_block_cnt = 0;

  if (!mod) {
    LOG_ERR("Vendor model not found");
    return;
  } else if (!mod->pub || !mod->pub->msg) {
    LOG_ERR("Model has no pub defined");
    return;
  }

  // Wait for mesh to be initialized and provisioned
  while (!bt_mesh_is_provisioned()) {
    k_sleep(K_SECONDS(1));
  }
  while (mod->pub->addr == BT_MESH_ADDR_UNASSIGNED ||
         mod->pub->addr == BT_MESH_ADDR_ALL_NODES) {
    LOG_WRN("Destination address not set. Waiting for Gateway Update...");
    mesh_request_gateway_addr();
    k_sleep(K_SECONDS(30));
  }

  while (1) {
    int64_t q_wait_start_ms = k_uptime_get();
    k_msgq_get(&mesh_tx_msgq, &tx_data, K_FOREVER);
    int64_t after_get_ms = k_uptime_get();
    uint32_t queue_wait_ms = (uint32_t)(after_get_ms - q_wait_start_ms);
    LOG_INF("Mesh TX thread got frame");

    int64_t frame_start_ms = after_get_ms;

    tx_stats_mesh_frame_attempted();

    if (!bt_mesh_is_provisioned()) {
      tx_stats_mesh_frame_failed();
      continue;
    }
    // Check if we are trying to send segmented data to a broadcast/group
    // address Segmented messages (required for len > 11) are NOT allowed to
    // group addresses
    if (tx_data.header.size > 5 && (BT_MESH_ADDR_IS_GROUP(mod->pub->addr) ||
                                    mod->pub->addr == BT_MESH_ADDR_ALL_NODES)) {
      LOG_WRN("Cannot send large frame (>11bytes aka. segmented) to "
              "Broadcast/Group address - waiting for Gateway Update...");
      tx_stats_mesh_frame_failed();
      continue;
    }

    size_t total_sent = 0;
    uint16_t block_idx =
        0; // must be dividable by 2, as the number send is for each two blocks

    bool full_frame_sent = true;

    LOG_INF("--> TX message (start)");

    k_mutex_lock(&mesh_pub_mutex, K_FOREVER);
    while (total_sent < BLE_PCKT_SEND_SIZE) {
      int64_t block_prep_start_ms = k_uptime_get();
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
      mod->pub->ttl = CONFIG_BT_MESH_DEFAULT_TTL;

      struct bt_mesh_msg_ctx ctx = {
          .addr = mod->pub->addr,
          .app_idx = mod->pub->key,
          .send_ttl = CONFIG_BT_MESH_DEFAULT_TTL,
      };

      uint32_t block_prep_ms = (uint32_t)(k_uptime_get() - block_prep_start_ms);

      LOG_INF("-->     block %d (%d)", block_idx, header.timestamp);
      int64_t send_call_start_ms = k_uptime_get();
      int err = bt_mesh_model_send(mod, &ctx, mod->pub->msg, &send_cb, NULL);
      uint32_t send_call_ms = (uint32_t)(k_uptime_get() - send_call_start_ms);
      if (err) {
        LOG_WRN("Sending failed at block %d: %d", block_idx, err);
        full_frame_sent = false;
        break;
      } else {
        int64_t wait_end_start_ms = k_uptime_get();
        k_sem_take(&mesh_send_sem, K_FOREVER);
        uint32_t wait_end_ms = (uint32_t)(k_uptime_get() - wait_end_start_ms);
        int end_err = (int)atomic_get(&mesh_last_send_err);
        if (end_err)
        {
          LOG_WRN("Send callback reported failure at block %d: %d", block_idx,
                  end_err);
          full_frame_sent = false;
          break;
        }
        LOG_INF("        block %d (%d) successfully sent", block_idx,
                header.timestamp);
        total_sent += bytes_to_send;
        block_idx += blocks_to_send;

        /* Update timing aggregates (only on successful block completion). */
        sum_block_prep_ms += block_prep_ms;
        sum_block_send_call_ms += send_call_ms;
        sum_block_wait_end_ms += wait_end_ms;
        if (wait_end_ms > max_block_wait_end_ms)
        {
          max_block_wait_end_ms = wait_end_ms;
        }
        timing_block_cnt++;
      }

      // Yield to let the stack process
      k_yield();
    }
    k_mutex_unlock(&mesh_pub_mutex);

    if (full_frame_sent && total_sent >= BLE_PCKT_SEND_SIZE)
    {
      tx_stats_mesh_frame_completed();
    }
    else
    {
      tx_stats_mesh_frame_failed();
    }

    /* Frame-level timing accounting + periodic summary. */
    uint32_t frame_total_ms = (uint32_t)(k_uptime_get() - frame_start_ms);
    sum_queue_wait_ms += queue_wait_ms;
    sum_frame_total_ms += frame_total_ms;
    if (frame_total_ms > max_frame_total_ms)
    {
      max_frame_total_ms = frame_total_ms;
    }
    timing_frames++;

    if ((timing_frames % MESH_TX_TIMING_LOG_EVERY_FRAMES) == 0)
    {
      uint32_t avg_queue_wait_ms =
          (uint32_t)(sum_queue_wait_ms / (uint64_t)timing_frames);
      uint32_t avg_frame_total_ms =
          (uint32_t)(sum_frame_total_ms / (uint64_t)timing_frames);

      uint32_t avg_block_prep_ms = 0;
      uint32_t avg_block_send_call_ms = 0;
      uint32_t avg_block_wait_end_ms = 0;
      if (timing_block_cnt > 0)
      {
        avg_block_prep_ms = (uint32_t)(sum_block_prep_ms / MESH_TX_TIMING_LOG_EVERY_FRAMES);
        avg_block_send_call_ms =
            (uint32_t)(sum_block_send_call_ms / MESH_TX_TIMING_LOG_EVERY_FRAMES);
        avg_block_wait_end_ms = (uint32_t)(sum_block_wait_end_ms / MESH_TX_TIMING_LOG_EVERY_FRAMES);
      }

      LOG_WRN(
          "MeshTX timing (last %u frames, %u total): avgFrame=%ums maxFrame=%ums avgQWait=%ums blocks=%u avgPrep=%ums avgSendCall=%ums avgWaitEnd=%ums maxWaitEnd=%ums",
          MESH_TX_TIMING_LOG_EVERY_FRAMES, timing_frames, avg_frame_total_ms, max_frame_total_ms,
          avg_queue_wait_ms, timing_block_cnt, avg_block_prep_ms,
          avg_block_send_call_ms, avg_block_wait_end_ms, max_block_wait_end_ms);
      sum_block_prep_ms = 0;
      sum_block_send_call_ms = 0;
      sum_block_wait_end_ms = 0;
      max_block_wait_end_ms = 0;
    }
  }
}

static const struct bt_mesh_model_op vnd_model_op[] = {
    {BT_MESH_VND_OP_WULPUS_FRAMECHUNK,
     BT_MESH_LEN_MIN(sizeof(frame_chunk_header)), mesh_receiving_data_chunk},
    {BT_MESH_VND_OP_WULPUS_START_CONFIG, BT_MESH_LEN_MIN(6),
     mesh_receiving_start_config},
    {BT_MESH_VND_OP_WULPUS_GATEWAY_UPDATE, BT_MESH_LEN_MIN(2),
     mesh_receiving_gateway_update},
    {BT_MESH_VND_OP_WULPUS_GATEWAY_REQ, BT_MESH_LEN_MIN(1),
     mesh_receiving_gateway_req},
    BT_MESH_MODEL_OP_END,
};

static const struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0,
                 BT_MESH_MODEL_LIST(
                     BT_MESH_MODEL_CFG_SRV, BT_MESH_MODEL_CFG_CLI(&cfg_cli),
                     BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub)),
                 BT_MESH_MODEL_LIST(BT_MESH_MODEL_VND(
                     BT_MESH_VND_ID, BT_MESH_VND_MODEL_ID_WULPUS, vnd_model_op,
                     &vnd_model_pub, NULL))),
    BT_MESH_ELEM(1, BT_MESH_MODEL_LIST(BT_MESH_MODEL_TIME_SRV(&time_srv)),
                 BT_MESH_MODEL_LIST()),
    BT_MESH_ELEM(2, BT_MESH_MODEL_LIST(BT_MESH_MODEL_TIME_CLI(&time_cli)),
                 BT_MESH_MODEL_LIST()),
};

const struct bt_mesh_comp comp = {
    .cid = BT_MESH_VND_ID,
    .elem_count = ARRAY_SIZE(elements),
    .elem = elements,
};

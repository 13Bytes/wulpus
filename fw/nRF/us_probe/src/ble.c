#include "ble.h"
#include "frame.h"
#include "helper.h"
#include "main.h"
#include "spi.h"
#include "tx_stats.h"
#include <bluetooth/services/nus.h>
#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_BT_MESH)
#include "mesh.h"
#endif

LOG_MODULE_REGISTER(ble);

// --- Globals ---
struct bt_conn *current_conn = NULL;
uint8_t ble_conn_id;
char device_name[DEVICE_NAME_MAX_LEN] = {DEVICE_NAME_BASE};
uint8_t device_name_len = sizeof(DEVICE_NAME_BASE) - 1;

K_MSGQ_DEFINE(ble_tx_msgq, sizeof(frame_chunk), BLE_TX_QUEUE_SIZE, 4);
static K_SEM_DEFINE(ble_tx_ready_sem, 1, 1);

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, device_name, strlen(DEVICE_NAME_BASE)),
};
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

// --- Functions ---

void ble_init_device_name(uint16_t addr)
{
  snprintf(device_name, DEVICE_NAME_MAX_LEN, "%s_%04X", DEVICE_NAME_BASE, addr);

  // Update the advertising data length
  device_name_len = strlen(device_name);
  ad[1].data_len = device_name_len;
  // Set the BT stack device name
  int err = bt_set_name(device_name);
  if (err)
  {
    LOG_ERR("Failed to set device name (err %d)", err);
  }
  else
  {
    LOG_INF("Device name set to: %s", device_name);
  }
}

int start_advertise(void)
{
  LOG_INF("starting advertising...");
  size_t id_count = 0xFF;
  struct bt_le_adv_param adv_params = *BT_LE_ADV_CONN_FAST_1;
  (void)bt_id_get(NULL, &id_count);
  if (id_count < CONFIG_BT_ID_MAX)
  {
    int id = bt_id_create(NULL, NULL);
    if (id < 0)
    {
      LOG_WRN("Unable to create a new identity for LBS (err %d) -> Using "
              "default one",
              id);
      ble_conn_id = BT_ID_DEFAULT;
    }
    else
    {
      ble_conn_id = id;
    }
  }
  else
  {
    ble_conn_id = BT_ID_DEFAULT;
  }
  adv_params.id = ble_conn_id;
  LOG_INF("Using BLE identity ID: %d", ble_conn_id);

  int err =
      bt_le_adv_start(&adv_params, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err)
  {
    LOG_ERR("Advertising failed to start (err %d)", err);
    if (err == -ENOMEM)
    {
      LOG_ERR(
          " - No free connection objects available for connectable advertiser");
    }
    else if (err == -ECONNREFUSED)
    {
      LOG_ERR(" - Connection refused - too many connections?");
    }
  }
  return err;
}

static void bt_received(struct bt_conn *const conn, const uint8_t *const data,
                        uint16_t len)
{
  LOG_INF("Received data over BLE (NUS). Len: %d", len);
  apply_config(data, len);

#if IS_ENABLED(CONFIG_BT_MESH)
  mesh_publish_config(data, len);
#endif
}

static void update_phy(struct bt_conn *conn)
{
  int err;
  const struct bt_conn_le_phy_param preferred_phy = {
      .options = BT_CONN_LE_PHY_OPT_NONE,
      .pref_tx_phy = BT_GAP_LE_PHY_2M,
      .pref_rx_phy = BT_GAP_LE_PHY_2M};

  // Retry PHY update if controller is busy
  for (unsigned i = 0; i < 5; i++)
  {
    err = bt_conn_le_phy_update(conn, &preferred_phy);
    if (err == 0)
    {
      LOG_INF("PHY update initiated successfully");
      return;
    }
    LOG_WRN("bt_conn_le_phy_update() failed (err %d), attempt %d/5", err, i + 1);
    k_sleep(K_MSEC(200 + (i * 100))); // Backoff
  }
  LOG_ERR("bt_conn_le_phy_update() failed after retries");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
  if (err)
  {
    LOG_ERR("Connection failed (err %u)", err);
    return;
  }
  struct bt_conn_info info;
  bt_conn_get_info(conn, &info);

  if (info.id == ble_conn_id)
  {
    LOG_INF("BLE NUS connection established");
    current_conn = bt_conn_ref(conn);
    k_sleep(K_MSEC(1000)); // wait a bit for connection to stabilize
    update_phy(conn);
    k_sleep(K_MSEC(100)); // wait a bit for PHY update to take effect
    LOG_INF("Connection interval: %d units (x1.25 for ms)", info.le.interval);

#if IS_ENABLED(CONFIG_BT_MESH)
    LOG_INF("It seems like I'm now the gateway - broadcasting my address to the Mesh");
    mesh_set_time_authority();
    mesh_publish_self_gateway();
#else
    LOG_INF("It seems like I'm now the gateway (BLE-only mode)");
#endif
  }
  else
  {
    LOG_INF("BLE Mesh/Other connection established (id: %d)", info.id);
  }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
  LOG_INF("BLE disconnected (reason %u)", reason);

  if (current_conn == conn)
  {
    LOG_INF("NUS Connection disconnected");
    bt_conn_unref(current_conn);
    current_conn = NULL;
    gpio_pin_set_dt(&ble_cnfg_ready, 0);
  }
  else
  {
    LOG_INF("Non-NUS connection disconnected");
  }
}

static void bt_sent(struct bt_conn *conn)
{
  // Called when BLE radio finishes transmitting
  k_sem_give(&ble_tx_ready_sem);
}

static void bt_recycle(void)
{
  LOG_INF("Disconnect complete! Restarting advertisement...");
  start_advertise();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = bt_recycle,
};
struct bt_nus_cb nus_callbacks = {
    .received = bt_received,
    .sent = bt_sent,
};

void ble_tx_thread(void)
{
  LOG_INF("BLE TX thread spawned and waiting for data to send...");
  static frame_chunk tx_data; // static to reduce stack usage
  static int64_t last_success_ts_ms =
      0; // time of last successful full-frame send

  int slow_counter = 0;
  int64_t slow_last_send_time_ms = k_uptime_get();

  while (1)
  {
    k_msgq_get(&ble_tx_msgq, &tx_data, K_FOREVER);
    LOG_INF("BLE TX thread got frame (addr %04x, len %d) (waited %lldms for queue)",
            tx_data.header.addr, tx_data.header.size, k_uptime_get() - last_gpio_interrupt_time);
    if (tx_data.header.size != BLE_PCKT_SEND_SIZE)
    {
      LOG_ERR("Unexpected frame size %d (expected %d)", tx_data.header.size,
              BLE_PCKT_SEND_SIZE);
      continue;
    }

    tx_stats_ble_frame_attempted();

    // Check if we have a valid BLE connection
    if (current_conn == NULL)
    {
      LOG_WRN("No active BLE connection - dropping frame");
      tx_stats_ble_frame_failed();
      continue;
    }
    // Send the frame in 201 byte chunks (202 bytes for the first to signalize
    // start, the last byte is irrelevant)
    bool full_frame_sent = true;
    uint8_t buf[BLE_HEADER_SZE + BLE_MAX_PAYLOAD_SIZE];

    ble_msg_header *header = (ble_msg_header *)buf;
    header->timestamp = tx_data.header.timestamp;
    header->length = tx_data.header.size + BLE_HEADER_SZE;
    header->addr = tx_data.header.addr;

    memcpy(buf + BLE_HEADER_SZE, tx_data.data, tx_data.header.size);

    for (unsigned i = 0; i < CHUNKS_PER_FRAME; i++)
    {
      int err;
      unsigned retries = 5;
      do
      {
        size_t const message_len =
            MIN((BLE_HEADER_SZE + tx_data.header.size) - (i * BYTES_PR_XFER_TX),
                BYTES_PR_XFER_TX);
        if (message_len > bt_nus_get_mtu(current_conn))
        {
          LOG_WRN("max BLE message len %d overexceeded! (attempt to send: %d)",
                  bt_nus_get_mtu(current_conn), message_len);
        }

        err =
            bt_nus_send(current_conn, &buf[i * BYTES_PR_XFER_TX], message_len);
        if (err == -ENOBUFS || err == -EAGAIN)
        {
          /* Controller/host back-pressure: wait a bit and retry */
          LOG_DBG("BLE backpressure on chunk %u, retrying (%u left)...", i,
                  retries);
          k_yield();
          continue;
        }
        else if (err)
        {
          LOG_WRN("BLE send failed (chunk %u) with err: %d - retrying (%u "
                  "left)....",
                  i, err, retries);
          k_sleep(K_USEC(50));
          continue;
        }
        else
        {
          // success
          break;
        }
      } while (--retries > 0);

      if (err)
      {
        LOG_WRN("BLE: All retries failed to send chunk %u (err %d)", i, err);
        full_frame_sent = false;
        break; // drop remaining part of message
      }
    }

    if (full_frame_sent)
    {
      tx_stats_ble_frame_completed();
      int64_t now_ms = k_uptime_get();
      if (last_success_ts_ms != 0)
      {
        int64_t delta_ms = now_ms - last_success_ts_ms;
        LOG_INF("BLE TX: full frame sent. dt since last success: %lld ms",
                (long long)delta_ms);
      }
      else
      {
        LOG_INF("BLE TX: full frame sent. First successful frame");
      }
      last_success_ts_ms = now_ms;

      slow_counter++;
      if (slow_counter % 20 == 0)
      {
        int64_t time_since_last_slow_ms = now_ms - slow_last_send_time_ms;
        uint32_t fps = 0;
        if (time_since_last_slow_ms <= 0)
        {
          fps =
              (uint32_t)(((uint64_t)20 * 1000U + time_since_last_slow_ms / 2) /
                         (uint64_t)time_since_last_slow_ms);
        }
        LOG_WRN("BLE sent 20 full frames. Average rate: %u fps (dt=%lld ms)",
                fps, (long long)time_since_last_slow_ms);
        slow_last_send_time_ms = now_ms;
      }
    }
    else
    {
      tx_stats_ble_frame_failed();
    }
  }
}

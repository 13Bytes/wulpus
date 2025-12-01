#include "ble.h"
#include "main.h"
#include "mesh.h"
#include "helper.h"
#include "spi.h"
#include <bluetooth/services/nus.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(ble);

// --- Globals ---
struct bt_conn *current_conn = NULL;
uint8_t ble_conn_id;
char device_name[DEVICE_NAME_MAX_LEN] = {DEVICE_NAME_BASE};
uint8_t device_name_len = sizeof(DEVICE_NAME_BASE) - 1;

K_MSGQ_DEFINE(ble_tx_msgq, sizeof(struct ble_data_t), BLE_TX_QUEUE_SIZE, 4);
static K_SEM_DEFINE(ble_tx_ready_sem, 1, 1);

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, device_name, strlen(DEVICE_NAME_BASE)),
};
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};

// --- Functions ---

void ble_init_device_name(const uint8_t *dev_uuid)
{
  // Use last 2 bytes of device UUID to create unique name
  // Format: WULPUS_PROBE_AABB (where AA and BB are hex values)
  snprintf(device_name, DEVICE_NAME_MAX_LEN, "%s_%02X%02X",
           DEVICE_NAME_BASE, dev_uuid[14], dev_uuid[15]);
  device_name_len = strlen(device_name);

  // Update the advertising data length
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
    ble_conn_id = BT_ID_DEFAULT + 1;
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

static void bt_received(struct bt_conn *conn, const uint8_t *const data,
                        uint16_t len)
{
  LOG_INF("Received data over BLE (NUS). Len: %d", len);
  apply_config(data, len);
  mesh_publish_config(data, len);
}

static void update_phy(struct bt_conn *conn)
{
  int err;
  const struct bt_conn_le_phy_param preferred_phy = {
      .options = BT_CONN_LE_PHY_OPT_NONE,
      .pref_tx_phy = BT_GAP_LE_PHY_2M,
      .pref_rx_phy = BT_GAP_LE_PHY_2M};
  err = bt_conn_le_phy_update(conn, &preferred_phy);
  if (err)
  {
    LOG_ERR("bt_conn_le_phy_update() returned %d", err);
  }
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
    update_phy(conn);
    k_sleep(K_MSEC(100)); // wait a bit for connection to stabilize
    LOG_INF("Connection interval: %d units (x1.25 for ms)", info.le.interval);
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
  static struct ble_data_t tx_data; // static to reduce stack usage
  static int64_t last_success_ts_ms =
      0; // time of last successful full-frame send

  int slow_counter = 0;
  int64_t slow_last_send_time_ms = k_uptime_get();

  while (1)
  {
    k_msgq_get(&ble_tx_msgq, &tx_data, K_FOREVER);
    LOG_INF("BLE TX thread got frame (len %d) (waited %lldms for queue)",
            tx_data.len, k_uptime_get() - last_gpio_interrupt_time);
    if (tx_data.len != BLE_PCKT_SEND_SIZE)
    {
      LOG_ERR("Unexpected frame size %d (expected %d)", tx_data.len,
              BLE_PCKT_SEND_SIZE);
      continue;
    }

    // Check if we have a valid BLE connection
    if (current_conn == NULL)
    {
      LOG_WRN("No active BLE connection - dropping frame");
      continue;
    }
    // Send the frame in 201 byte chunks (202 bytes for the first to signalize
    // start, the last byte is irrelevant)
    bool full_frame_sent = true;
    for (unsigned i = 0; i < CHUNKS_PER_FRAME; i++)
    {
      uint16_t message_len = (i > 0) ? BYTES_PR_XFER_RX : BYTES_PR_XFER_RX + 1;
      int err;
      unsigned retries = 10;
      do
      {
        err = bt_nus_send(current_conn, &tx_data.data[i * BYTES_PR_XFER_RX],
                          message_len);
        if (err == -ENOBUFS || err == -EAGAIN)
        {
          /* Controller/host back-pressure: wait a bit and retry */
          LOG_DBG("BLE backpressure on chunk %u, retrying (%u left)...", i,
                  retries);
          k_yield();
          continue;
        }
        if (err)
        {
          LOG_WRN("BLE send failed (chunk %u) with err: %d - retrying (%u "
                  "left)....",
                  i, err, retries);
          k_sleep(K_USEC(50));
          continue;
        }
        // Success - break out of retry loop
        break;
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
      int64_t now_ms = k_uptime_get();
      if (last_success_ts_ms != 0)
      {
        int64_t delta_ms = now_ms - last_success_ts_ms;
        LOG_INF("BLE: full frame sent. dt since last success: %lld ms",
                (long long)delta_ms);
      }
      else
      {
        LOG_INF("BLE: full frame sent. First successful frame");
      }
      last_success_ts_ms = now_ms;

      slow_counter++;
      if (slow_counter % 20 == 0)
      {
        int64_t time_since_last_slow_ms = now_ms - slow_last_send_time_ms;
        if (time_since_last_slow_ms <= 0)
        {
          LOG_WRN("BLE: Sent 20 full frames. Average rate unavailable "
                  "(dt=%lld ms)",
                  (long long)time_since_last_slow_ms);
        }
        else
        {
          uint32_t fps =
              (uint32_t)(((uint64_t)20 * 1000U + time_since_last_slow_ms / 2) /
                         (uint64_t)time_since_last_slow_ms);
          LOG_WRN("BLE: Sent 20 full frames. Average rate: %u fps", fps);
        }
        slow_last_send_time_ms = now_ms;
      }
    }
  }
}

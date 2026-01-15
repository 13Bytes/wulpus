#include "helper.h"
#include "main.h"
#include "spi.h"
#include "testfunctions.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(helper);

bool own_message(const struct bt_mesh_model *model, const struct bt_mesh_msg_ctx *ctx)
{
  const struct bt_mesh_elem *elem = bt_mesh_model_elem(model);
  return elem->rt->addr == ctx->addr;
}

void apply_config(uint8_t *const data, uint16_t len)
{
  LOG_INF("Applying new configuration. Len: %d", len);

  // Simulate job start/stop based on config length
  if (len == 0)
  {
    LOG_WRN("Empty configuration received; ignoring.");
    return;
  }
  if (data[0] == 0xfa)
  {
    LOG_INF("Starting mesh job as per configuration.");
    set_mesh_job_state(true);

    uint16_t interval_clcks = data[3] | (data[4] << 8);
    mesh_rand_sender_period_ms = (uint32_t)interval_clcks * 2000 / 65535;
    LOG_INF("(interval set to %d ms)", mesh_rand_sender_period_ms);
  }
  else
  {
    LOG_INF("Stopping mesh job as per configuration.");
    set_mesh_job_state(false);
  }

  LOG_HEXDUMP_INF(data, len, "NUS RX");
  k_mutex_lock(&tx_buffer_mutex, K_FOREVER);
  memset(m_tx_buffer, 0, BYTES_PR_XFER_TX * CHUNKS_PER_FRAME); // Clear old data
  memcpy(m_tx_buffer, data, len);
  k_mutex_unlock(&tx_buffer_mutex);

  LOG_INF("Inform MSP about new configuration (ble_cnfg_ready to high)");
  int err = gpio_pin_set_dt(&ble_cnfg_ready, 1);
  if (err)
  {
    LOG_ERR("Failed to set BLE configuration ready pin: %d", err);
  }
}
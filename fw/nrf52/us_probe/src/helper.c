#include "helper.h"
#include "main.h"
#include "spi.h"
#include "testfunctions.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(helper);

/**
 * Returns true if the specified address is an address of the local element.
 */
bool address_is_local(const struct bt_mesh_elem *elem, uint16_t addr)
{
  return elem->rt->addr == addr;
}

void apply_config(uint8_t *const data, uint16_t len)
{
  LOG_INF("Applying new configuration. Len: %d", len);

  // Simulate job start/stop based on config length
  if (len > 0)
  {
    set_mesh_job_state(true);
  }
  else
  {
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
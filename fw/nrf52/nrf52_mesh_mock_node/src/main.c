#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main);

// --- GPIOs ---
// Assuming LED0 is available on the board
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

// --- Device Name ---
#define DEVICE_NAME_BASE "WULPUS_PROBE"
#define DEVICE_NAME_MAX_LEN 32
static char device_name[DEVICE_NAME_MAX_LEN] = {DEVICE_NAME_BASE};
static uint8_t dev_uuid[16];

static void ble_init_device_name(const uint8_t *dev_uuid)
{
  // Use last 2 bytes of device UUID to create unique name
  // Format: WULPUS_PROBE_AABB (where AA and BB are hex values)
  snprintf(device_name, DEVICE_NAME_MAX_LEN, "%s_%02X%02X", DEVICE_NAME_BASE,
           dev_uuid[14], dev_uuid[15]);

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
  k_sleep(K_MSEC(500));
}

int main(void)
{
  int err;

  LOG_INF("Starting WULPUS Simple Mesh Node");

  // Initialize GPIO
  if (!gpio_is_ready_dt(&led))
  {
    LOG_ERR("LED GPIO not ready");
    return 0;
  }
  gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);

  // Initialize Bluetooth
  err = bt_enable(NULL);
  if (err)
  {
    LOG_ERR("Bluetooth init failed (err %d)", err);
    return 0;
  }
  LOG_INF("Bluetooth initialized");

  // --- UUID and Name ---
  if (IS_ENABLED(CONFIG_HWINFO))
  {
    size_t id_len = hwinfo_get_device_id(dev_uuid, 16);
    // If device ID is shorter than UUID size, fill rest of buffer with inverted
    // device ID.
    for (size_t i = id_len; i < 16; i++)
    {
      dev_uuid[i] = dev_uuid[i % id_len] ^ 0xff;
    }
    dev_uuid[6] = (dev_uuid[6] & BIT_MASK(4)) | BIT(6);
    dev_uuid[8] = (dev_uuid[8] & BIT_MASK(6)) | BIT(7);
  }
  else
  {
    LOG_ERR("HWINFO not enabled - using default UUID");
    dev_uuid[0] = 0xdd;
    dev_uuid[1] = 0xdd;
  }

  LOG_INF("Initializing device name...");
  ble_init_device_name(dev_uuid);

  // Initialize Mesh
  mesh_init();

  // --- Self Provisioning ---
  char uuid_str[5] = {0};
  snprintf(uuid_str, sizeof(uuid_str), "%02X%02X", dev_uuid[14], dev_uuid[15]);
  uint16_t addr = (uint16_t)strtol(uuid_str, NULL, 16) & 0x7fff;
  if (addr == 0)
  {
    addr = 1;
  }
  LOG_INF("Using address: 0x%04x (derived from bytes 14-15: %02X%02X)", addr,
          dev_uuid[14], dev_uuid[15]);

  mesh_provision_self(addr);

  while (1)
  {
    gpio_pin_toggle_dt(&led);
    k_sleep(K_MSEC(1000));
  }
  return 0;
}

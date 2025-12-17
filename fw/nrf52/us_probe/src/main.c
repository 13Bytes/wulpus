#include <bluetooth/mesh/models.h>
#include <bluetooth/services/nus.h>
#include <stdlib.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "ble.h"
#include "helper.h"
#include "main.h"
#include "mesh.h"
#include "spi.h"
#include "testfunctions.h"

LOG_MODULE_REGISTER(main);

// --- GPIOs ---
const struct gpio_dt_spec ble_cnfg_ready =
    GPIO_DT_SPEC_GET(BLE_CNFG_READY_NODE, gpios);
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
const struct gpio_dt_spec data_ready = GPIO_DT_SPEC_GET(DATA_READY_NODE, gpios);
const struct gpio_dt_spec dbg_button_2 =
    GPIO_DT_SPEC_GET(DBG_BUTTON_2_NODE, gpios);
const struct gpio_dt_spec dbg_button_3 =
    GPIO_DT_SPEC_GET(DBG_BUTTON_3_NODE, gpios);

// callback for data-ready GPIO interrupt, triggering SPIM transfer
struct gpio_callback data_ready_cb;
struct gpio_callback dbg_button_2_cb;
struct gpio_callback dbg_button_3_cb;

int64_t last_gpio_interrupt_time = 0;
static void gpio_interrupt_handler(const struct device *dev,
                                   struct gpio_callback *cb, uint32_t pins)
{
    int64_t const dt_now = k_uptime_get();
    int64_t const delta = dt_now - last_gpio_interrupt_time;
    LOG_INF("GPIO interrupt - dt since last: %lldms", delta);
    LOG_DBG("GPIO interrupt data_ready: trigger SPI session thread");
    if (delta < MIN_INTERRUPT_INTERVAL_MS)
    {
        LOG_WRN("Ignoring spurious interrupt (dt=%lldms < %dms)", delta,
                MIN_INTERRUPT_INTERVAL_MS);
        return;
    }
    last_gpio_interrupt_time = dt_now;
    k_sem_give(&data_ready_trigger_sem);
}

static void button_2_work_handler(struct k_work *work)
{
    LOG_INF("Button 2 pressed");
    LOG_INF("Sending start-config");
    uint8_t mock_config[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    mesh_publish_config(mock_config, sizeof(mock_config));
    apply_config(mock_config, sizeof(mock_config));
}

K_WORK_DEFINE(button_2_work, button_2_work_handler);

static void button_3_work_handler(struct k_work *work)
{
    LOG_INF("Button 3 pressed");
    LOG_INF("Sending stop-config");
    uint8_t mock_config[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    mesh_publish_config(mock_config, sizeof(mock_config));
    apply_config(mock_config, sizeof(mock_config));
}

K_WORK_DEFINE(button_3_work, button_3_work_handler);

static void dbg_button_2_handler(const struct device *dev,
                                 struct gpio_callback *cb, uint32_t pins)
{
    k_work_submit(&button_2_work);
}

static void dbg_button_3_handler(const struct device *dev,
                                 struct gpio_callback *cb, uint32_t pins)
{
    k_work_submit(&button_3_work);
}

// --- Mesh -------------------------------------
uint16_t addr;
static const uint16_t net_idx;
static const uint32_t iv_index;
static uint8_t flags;

// --- MAIN -------------------------------------
K_THREAD_DEFINE(ble_tx_thread_id, 2048, ble_tx_thread, NULL, NULL, NULL,
                BLE_TASK_PRIO, 0, 0);
K_THREAD_DEFINE(mesh_tx_thread_id, 4096 * 2, mesh_tx_thread, NULL, NULL, NULL,
                MESH_TX_TASK_PRIO, 0, 0);
K_THREAD_DEFINE(spi_session_thread_id, 2048, spi_session_thread, NULL, NULL,
                NULL, SPI_TASK_PRIO, 0, 0);
K_THREAD_DEFINE(mesh_rand_sender_thread_id, 2048, mesh_rand_sender_thread, NULL,
                NULL, NULL, 7, 0, 0);

int main(void)
{
    LOG_WRN("Start-delay 5s");
    k_sleep(K_MSEC(5000));

    LOG_INF("Starting WULPUS nRF Firmware (%s)", CONFIG_BOARD);
    int err;

    LOG_INF("Initializing GPIOs...");
    if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&data_ready) ||
        !gpio_is_ready_dt(&ble_cnfg_ready) || !gpio_is_ready_dt(&dbg_button_2) ||
        !gpio_is_ready_dt(&dbg_button_3))
    {
        LOG_ERR("GPIO devices not ready.");
        return 0;
    }
    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_LOW) |
          gpio_pin_configure_dt(&ble_cnfg_ready, GPIO_OUTPUT_LOW) |
          gpio_pin_configure_dt(&data_ready, GPIO_INPUT) |
          gpio_pin_configure_dt(&dbg_button_2, GPIO_INPUT) |
          gpio_pin_configure_dt(&dbg_button_3, GPIO_INPUT);
    if (err < 0)
    {
        LOG_ERR("Error configuring GPIO pins");
        return 0;
    }

    // Register callback first, then enable interrupt to avoid missing edges
    // during setup
    LOG_INF("Setting up data-ready callback");
    gpio_init_callback(&data_ready_cb, gpio_interrupt_handler,
                       BIT(data_ready.pin));
    err = gpio_add_callback_dt(&data_ready, &data_ready_cb);
    if (err)
    {
        LOG_ERR("Error setting up callback: %d", err);
        return err;
    }

    gpio_init_callback(&dbg_button_2_cb, dbg_button_2_handler,
                       BIT(dbg_button_2.pin));
    gpio_add_callback_dt(&dbg_button_2, &dbg_button_2_cb);
    gpio_pin_interrupt_configure_dt(&dbg_button_2, GPIO_INT_EDGE_TO_ACTIVE);

    gpio_init_callback(&dbg_button_3_cb, dbg_button_3_handler,
                       BIT(dbg_button_3.pin));
    gpio_add_callback_dt(&dbg_button_3, &dbg_button_3_cb);
    gpio_pin_interrupt_configure_dt(&dbg_button_3, GPIO_INT_EDGE_TO_ACTIVE);

    LOG_INF("Initializing settings subsystem");
    err = settings_subsys_init();
    if (err)
    {
        LOG_ERR("Failed to initialize settings subsystem: %d\n", err);
        return err;
    }

    LOG_INF("Setting up Bluetooth LE");
    err = bt_nus_init(&nus_callbacks);
    if (err)
    {
        LOG_ERR("Failed to register Bluetooth NUS callback: %d\n", err);
        return err;
    }
    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Failed to enable Bluetooth: %d\n", err);
        return err;
    }

    LOG_INF("Reading config to prepare for Bluetooth LE Mesh");

    if (IS_ENABLED(CONFIG_HWINFO))
    {
        size_t id_len = hwinfo_get_device_id(dev_uuid, 16);
        if (!IS_ENABLED(CONFIG_BT_MESH_DK_LEGACY_UUID_GEN))
        {
            /* If device ID is shorter than UUID size, fill rest of buffer with
             * inverted device ID.
             */
            for (size_t i = id_len; i < 16; i++)
            {
                dev_uuid[i] = dev_uuid[i % id_len] ^ 0xff;
            }
        }
        dev_uuid[6] = (dev_uuid[6] & BIT_MASK(4)) | BIT(6);
        dev_uuid[8] = (dev_uuid[8] & BIT_MASK(6)) | BIT(7);
    }
    else
    {
        LOG_ERR("HWINFO not enable - fallback to default UUID!");
        dev_uuid[0] = 0xdd;
        dev_uuid[1] = 0xdd;
    }
    LOG_INF(
        "Mesh Device UUID: "
        "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        dev_uuid[0], dev_uuid[1], dev_uuid[2], dev_uuid[3], dev_uuid[4],
        dev_uuid[5], dev_uuid[6], dev_uuid[7], dev_uuid[8], dev_uuid[9],
        dev_uuid[10], dev_uuid[11], dev_uuid[12], dev_uuid[13], dev_uuid[14],
        dev_uuid[15]);

    char uuid_str[5] = {0};
    snprintf(uuid_str, sizeof(uuid_str), "%02X%02X", dev_uuid[14], dev_uuid[15]);
    addr = (uint16_t)strtol(uuid_str, NULL, 16) & 0x7fff;
    if (addr == 0)
    {
        addr = 1;
    }
    LOG_INF("Using address: 0x%04x (derived from bytes 14-15: %02X%02X)", addr,
            dev_uuid[14], dev_uuid[15]);

    LOG_INF("Initializing GATT device name...");
    ble_init_device_name(addr);

    LOG_INF("Setting up BLE Mesh");
    err = bt_mesh_init(&prov, &comp);
    if (err)
    {
        LOG_ERR("Initializing mesh failed (err %d)\n", err);
        return err;
    }

    if (IS_ENABLED(CONFIG_BT_SETTINGS))
    {
        LOG_INF("restoring the Bluetooth state (e.g. pairing keys)");
        settings_load();
    }
    else
    {
        LOG_WRN("CONFIG_BT_SETTINGS not enabled - won't restore Bluetooth state");
    }

    err = bt_mesh_provision(net_key, net_idx, flags, iv_index, addr, dev_key);
    if (err == -EALREADY)
    {
        LOG_WRN("Using stored settings - configuration already exists");
        // Request gateway address since prov_complete won't be called
        mesh_request_gateway_addr();
    }
    else if (err)
    {
        LOG_ERR("Provisioning failed (err %d)", err);
        return err;
    }
    else
    {
        LOG_INF("Provisioning completed - configuration will be done in prov_complete callback");
    }

    /* This will be a no-op if settings_load() loaded provisioning info */
    bt_mesh_prov_enable(
        (bt_mesh_prov_bearer_t)(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT));

    LOG_INF("Mesh initialized");

    LOG_INF("Starting BLE advertisement");
    err = start_advertise();
    if (err)
    {
        return err;
    }

    us_spi_init();

    // Enable interrupt on rising edge (SPI thread already running)
    err = gpio_pin_interrupt_configure_dt(&data_ready, GPIO_INT_EDGE_TO_ACTIVE);
    if (err < 0)
    {
        LOG_ERR("Error configuring interrupt: %d", err);
        return 0;
    }

    // Initial check if pin is already active -> manually trigger the interrupt
    // (as edge has already passed)
    if (gpio_pin_get_dt(&data_ready) > 0)
    {
        LOG_INF(
            "MSP has data already ready. Triggering data_ready_cb manually once.");
        gpio_interrupt_handler(data_ready.port, &data_ready_cb,
                               BIT(data_ready.pin));
    }

    while (1)
    {
        k_sleep(K_MSEC(1000));
        gpio_pin_toggle_dt(&led);
    }

    return 0;
}

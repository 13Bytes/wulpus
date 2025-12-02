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

K_SEM_DEFINE(dbg_btn_2_sem, 0, 5);

static void dbg_button_2_handler(const struct device *dev,
                                 struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("Button 2 pressed");
    k_sem_give(&dbg_btn_2_sem);
}

static void dbg_button_3_handler(const struct device *dev,
                                 struct gpio_callback *cb, uint32_t pins)
{
    LOG_INF("Button 3 pressed");
    static struct frame_chunk tx_data;
    tx_data.header.timestamp = k_ticks_to_us_floor32(k_uptime_ticks());
    // Fill with some mock data
    for (int i = 0; i < BLE_PCKT_SEND_SIZE; i++)
    {
        tx_data.data[i] = i;
    }
    k_msgq_put(&mesh_tx_msgq, &tx_data, K_NO_WAIT);
}

void dbg_btn_2_thread(void)
{
    while (1)
    {
        k_sem_take(&dbg_btn_2_sem, K_FOREVER);
        uint8_t mock_config[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
        mesh_publish_config(mock_config, sizeof(mock_config));
        apply_config(mock_config, sizeof(mock_config));
    }
}

// --- Mesh -------------------------------------
uint16_t addr;
static const uint16_t net_idx;
static const uint16_t app_idx = {0x000};
static const uint32_t iv_index;
static uint8_t flags;

// --- MAIN -------------------------------------
K_THREAD_DEFINE(ble_tx_thread_id, 2048, ble_tx_thread, NULL, NULL, NULL,
                BLE_TASK_PRIO, 0, 0);
K_THREAD_DEFINE(mesh_tx_thread_id, 4096 * 2, mesh_tx_thread, NULL, NULL, NULL,
                MESH_TX_TASK_PRIO, 0, 0);
K_THREAD_DEFINE(spi_session_thread_id, 2048, spi_session_thread, NULL, NULL,
                NULL, SPI_TASK_PRIO, 0, 0);
K_THREAD_DEFINE(dbg_btn_2_thread_id, 4096 * 2, dbg_btn_2_thread, NULL, NULL,
                NULL, 7, 0, 0);
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
        LOG_ERR("HWINFO not enable - using default UUIDd");
        dev_uuid[0] = 0xdd;
        dev_uuid[1] = 0xdd;
    }
    // Initialize device name based on hardware ID
    LOG_INF("Initializing device name...");
    ble_init_device_name(dev_uuid);

    LOG_INF("Setting up Bluetooth LE Mesh");
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

    LOG_WRN("Provisioning Mesh (DEBUG)");
    LOG_INF(
        "Device UUID: "
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
    LOG_INF("AppKey bind status: %d", bind_status);
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
    LOG_INF("App bind status: %d", bind_status);
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

    /* Bind to Health model */
    err = bt_mesh_cfg_cli_mod_app_bind(net_idx, addr, addr, app_idx,
                                       BT_MESH_MODEL_ID_HEALTH_SRV, &bind_status);
    LOG_INF("App SIG bind status: %d", bind_status);
    if (err)
    {
        LOG_ERR("Failed to bind Health Model (err %d)", err);
    }
    else
    {
        LOG_INF("Health Model bound");
    }
    LOG_INF("Mesh configuration done");

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

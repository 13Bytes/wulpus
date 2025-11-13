#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <bluetooth/services/nus.h>
#include <nrfx_spim.h>
#include <nrfx_timer.h>
#include <nrfx_ppi.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_spim.h>

#include "main.h"
#include "testfunctions.h"

const struct gpio_dt_spec ble_cnfg_ready = GPIO_DT_SPEC_GET(BLE_CNFG_READY_NODE, gpios);
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);
const struct gpio_dt_spec data_ready = GPIO_DT_SPEC_GET(DATA_READY_NODE, gpios);

LOG_MODULE_REGISTER(main);

static K_MUTEX_DEFINE(tx_buffer_mutex);

// --- SPIM -------------------------------------
static uint8_t m_tx_buffer[BYTES_PR_XFER_TX * CHUNKS_PER_FRAME] = {0};
static uint8_t m_rx_buffer[BYTES_PR_XFER_RX * CHUNKS_PER_FRAME] = {0};

static const nrfx_spim_t spim_inst = NRFX_SPIM_INSTANCE(SPIM_INST_IDX);

// Timer to trigger SPI transfers at regular intervals (300µs)
static const nrfx_timer_t timer_spi_trigger = NRFX_TIMER_INSTANCE(3);
// Counter to track number of completed SPI transfers
static const nrfx_timer_t timer_counter = NRFX_TIMER_INSTANCE(4);

// PPI/DPPI channels
static nrf_ppi_channel_t ppi_timer_to_spi;
static nrf_ppi_channel_t ppi_spi_to_counter;

// Semaphore to serialize sessions triggered by the data-ready IRQ
static struct k_sem single_session;
// Semaphore signaled on SPI transfer completion
static struct k_sem spi_done_sem;
// Semaphore to trigger SPI session thread from GPIO interrupt
static K_SEM_DEFINE(data_ready_trigger_sem, 0, 1);

static void spim_handler(nrfx_spim_evt_t const *p_event, void *p_context)
{
    if (p_event->type == NRFX_SPIM_EVENT_DONE)
    {
        // Signal that transfer is complete
        k_sem_give(&spi_done_sem);
    }
}

static void us_spi_init(void)
{
    LOG_INF("Starting SPI initialization");
    nrfx_err_t err;

    // Initialize SPIM
    nrfx_spim_config_t spim_config = NRFX_SPIM_DEFAULT_CONFIG(SCK_PIN,
                                                              MOSI_PIN,
                                                              MISO_PIN,
                                                              SS_PIN);
    spim_config.frequency = NRFX_MHZ_TO_HZ(8);
    spim_config.mode = NRF_SPIM_MODE_1;
    spim_config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;
    spim_config.irq_priority = SPI_1_PRIO;
    err = nrfx_spim_init(&spim_inst, &spim_config, spim_handler, nullptr);
    if (err != NRFX_SUCCESS)
    {
        LOG_ERR("Failed to initialize SPIM instance %d with err: %d", SPIM_INST_IDX, err);
        return;
    }

    IRQ_DIRECT_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_SPIM_INST_GET(SPIM_INST_IDX)), IRQ_PRIO_LOWEST,
                       NRFX_SPIM_INST_HANDLER_GET(SPIM_INST_IDX), 0);
    irq_enable(NRFX_IRQ_NUMBER_GET(NRF_SPIM_INST_GET(SPIM_INST_IDX)));

    // Initialize semaphores
    k_sem_init(&spi_done_sem, 0, CHUNKS_PER_FRAME);
    k_sem_init(&single_session, 1, 1);

    LOG_INF("SPI init complete");
}

// --- Bluetooth LE -----------------------------
static K_SEM_DEFINE(ble_tx_ready_sem, 1, 1);
static struct bt_conn *current_conn;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
};
K_MSGQ_DEFINE(ble_tx_msgq, sizeof(struct ble_data_t), 15, 4);

int start_advertise(void)
{
    LOG_INF("starting advertising...");
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
    return err;
}

static void bt_received(struct bt_conn *conn, const uint8_t *const data, uint16_t len)
{
    LOG_INF("Received data over BLE (NUS). Len: %d", len);
    LOG_HEXDUMP_INF(data, len, "NUS RX");

    k_mutex_lock(&tx_buffer_mutex, K_FOREVER);
    memset(m_tx_buffer, 0, sizeof(m_tx_buffer)); // Clear old data
    memcpy(m_tx_buffer, data, len);
    k_mutex_unlock(&tx_buffer_mutex);

    LOG_INF("Inform MSP about new configuration (ble_cnfg_ready to high)");
    int err = gpio_pin_set_dt(&ble_cnfg_ready, 1);
    if (err)
    {
        LOG_ERR("Failed to set BLE configuration ready pin: %d", err);
    }
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
    LOG_INF("BLE connection established");
    current_conn = bt_conn_ref(conn);
    update_phy(conn);
    k_sleep(K_MSEC(100)); // wait a bit for connection to stabilize
    struct bt_conn_info info;
    bt_conn_get_info(conn, &info);
    LOG_INF("Connection interval: %d units (x1.25 for ms)", info.le.interval);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("BLE disconnected (reason %u)", reason);
    gpio_pin_set_dt(&ble_cnfg_ready, 0);
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

static struct gpio_callback data_ready_cb;

// SPI session thread - dedicated high-priority thread for SPI transfers
static void spi_session_thread(void)
{
    LOG_INF("SPI session thread spawned");

    while (1)
    {
        // Wait for GPIO interrupt to trigger a session
        k_sem_take(&data_ready_trigger_sem, K_FOREVER);

        LOG_INF("SPI session thread activated");

        // Ensure only one read session runs at a time (serialize sessions)
        if (k_sem_take(&single_session, K_NO_WAIT) != 0)
        {
            LOG_WRN("SPI session already in progress; skipping trigger");
            continue;
        }

        LOG_INF("Starting SPI session - single 804-byte transfer");

        // Single transfer for all 804 bytes (4 * 201)
        // Hardware limitation is 255 bytes per transfer, so we still need multiple calls
        // BUT we set up the descriptor once per session
        nrfx_spim_xfer_desc_t xfer = NRFX_SPIM_XFER_TRX(
            m_tx_buffer, BYTES_PR_XFER_TX,
            m_rx_buffer, BYTES_PR_XFER_RX);

        bool xfer_failed = false;
        for (int i = 0; i < CHUNKS_PER_FRAME; i++)
        {
            // Update pointers for this chunk (manual increment since HW has 255-byte limit)
            xfer.p_tx_buffer = &m_tx_buffer[i * BYTES_PR_XFER_TX];
            xfer.p_rx_buffer = &m_rx_buffer[i * BYTES_PR_XFER_RX];

            nrfx_err_t nerr = nrfx_spim_xfer(&spim_inst, &xfer, NRFX_SPIM_FLAG_REPEATED_XFER);
            if (nerr != NRFX_SUCCESS)
            {
                LOG_ERR("SPI xfer %d start failed: %d", i, nerr);
                xfer_failed = true;
                break;
            }

            // Wait for this chunk to complete
            if (k_sem_take(&spi_done_sem, K_MSEC(100)) != 0)
            {
                LOG_ERR("SPI xfer %d timeout", i);
                xfer_failed = true;
                break;
            }
        }

        if (!xfer_failed)
        {
            LOG_INF("SPI session complete");
            LOG_INF("SPI RX: SOF=0x%02X, tx_rx_id=%d, frame_nr=%d",
                    m_rx_buffer[0],
                    m_rx_buffer[1],
                    (m_rx_buffer[3] << 8) | m_rx_buffer[2]);

            bool all_zero = true;
            for (int i = 4; i < 100; i++)
            {
                if (m_rx_buffer[i] != 0)
                {
                    all_zero = false;
                    break;
                }
            }
            if (all_zero)
            {
                LOG_WRN("Frame appears to be empty/stopped - NOT enqueueing");
                k_sem_give(&single_session);
                continue; // Don't enqueue empty frames
            }

            // Build one full frame and enqueue as a single item
            ble_data_t tx_item = {};
            tx_item.len = BLE_PCKT_SEND_SIZE;
            memcpy(&tx_item.data, m_rx_buffer, BLE_PCKT_SEND_SIZE);

            uint32_t queue_used = 15 - k_msgq_num_free_get(&ble_tx_msgq);
            LOG_INF("BLE queue depth used: %d/15", queue_used);

            int qerr = k_msgq_put(&ble_tx_msgq, &tx_item, K_FOREVER);
            if (qerr != 0)
            {
                LOG_WRN("BLE TX queue full; dropping full frame (err %d)", qerr);
            }
        }
        // Release session gate
        k_sem_give(&single_session);
    }
}

int64_t last_gpio_interrupt_time{0};
static void gpio_interrupt_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    int64_t const dt_now = k_uptime_get();
    int64_t const delta = dt_now - last_gpio_interrupt_time;
    LOG_INF("GPIO interrupt - dt since last: %lldms", delta);
    LOG_DBG("GPIO interrupt data_ready: trigger SPI session thread");
    if (delta < MIN_INTERRUPT_INTERVAL_MS)
    {
        LOG_WRN("Ignoring spurious interrupt (dt=%lldms < %dms)",
                delta, MIN_INTERRUPT_INTERVAL_MS);
        return;
    }
    last_gpio_interrupt_time = dt_now;
    k_sem_give(&data_ready_trigger_sem);
}

static void ble_tx_thread(void)
{
    LOG_INF("BLE TX thread spawned and waiting for data to send...");
    static struct ble_data_t tx_data;      // static to reduce stack usage
    static int64_t last_success_ts_ms = 0; // time of last successful full-frame send

    int slow_counter = 0;
    int64_t slow_last_send_time_ms = k_uptime_get();
    while (1)
    {
        k_msgq_get(&ble_tx_msgq, &tx_data, K_FOREVER);
        LOG_INF("BLE TX thread got frame (len %d) (waited %lldms for queue)", tx_data.len, k_uptime_get() - last_gpio_interrupt_time);
        if (tx_data.len != BLE_PCKT_SEND_SIZE)
        {
            LOG_ERR("Unexpected frame size %d (expected %d)", tx_data.len, BLE_PCKT_SEND_SIZE);
            continue;
        }
        // Send the frame in 201 byte chunks (202 bytes for the first to signalize start, the last byte is irrelevant)
        bool full_frame_sent = true;
        for (unsigned i{0}; i < CHUNKS_PER_FRAME; i++)
        {
            uint16_t message_len = (i > 0) ? BYTES_PR_XFER_RX : BYTES_PR_XFER_RX + 1;
            int err;
            unsigned retries = 10;
            do
            {
                err = bt_nus_send(current_conn, &tx_data.data[i * BYTES_PR_XFER_RX], message_len);
                if (err == -ENOBUFS || err == -EAGAIN)
                {
                    /* Controller/host back-pressure: wait a bit and retry */
                    LOG_DBG("BLE backpressure on chunk %u, retrying (%u left)...", i, retries);
                    k_yield();
                    continue;
                }
                if (err)
                {
                    LOG_WRN("BLE send failed (chunk %u) with err: %d - retrying (%u left)....", i, err, retries);
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
                LOG_INF("BLE: full frame sent. dt since last success: %lld ms", (long long)delta_ms);
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
                LOG_WRN("BLE: Sent 20 full frames. Average rate: %.2f fps",
                        (double)(20.0 * 1000.0) / time_since_last_slow_ms);
                slow_last_send_time_ms = now_ms;
            }
        }
    }
}

// --- MAIN -------------------------------------
K_THREAD_DEFINE(ble_tx_thread_id, 2048, ble_tx_thread, NULL, NULL, NULL, BLE_TASK_PRIO, 0, 0);
K_THREAD_DEFINE(spi_session_thread_id, 2048, spi_session_thread, NULL, NULL, NULL, SPI_TASK_PRIO, 0, 0);
// K_THREAD_DEFINE(ble_random_send_thread, 1024, rand_sender_thread, NULL, NULL, NULL, 7, 0, 0);
int main(void)
{
    LOG_WRN("Start-delay 5s");
    k_sleep(K_MSEC(5000));

    LOG_INF("Starting WULPUS nRF Firmware of %s (%s)", DEVICE_NAME, CONFIG_BOARD);
    int err;

    LOG_INF("Initializing GPIOs...");
    if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&data_ready) || !gpio_is_ready_dt(&ble_cnfg_ready))
    {
        LOG_ERR("GPIO devices not ready.");
        return 0;
    }
    err =
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_LOW) |
        gpio_pin_configure_dt(&ble_cnfg_ready, GPIO_OUTPUT_LOW) |
        gpio_pin_configure_dt(&data_ready, GPIO_INPUT);
    if (err < 0)
    {
        LOG_ERR("Error configuring GPIO pins");
        return 0;
    }

    // Register callback first, then enable interrupt to avoid missing edges during setup
    LOG_INF("Setting up data-ready callback");
    gpio_init_callback(&data_ready_cb, gpio_interrupt_handler, BIT(data_ready.pin));
    err = gpio_add_callback_dt(&data_ready, &data_ready_cb);
    if (err)
    {
        LOG_ERR("Error setting up callback: %d", err);
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

    // Initial check if pin is already active -> manually trigger the interrupt (as edge has already passed)
    if (gpio_pin_get_dt(&data_ready) > 0)
    {
        LOG_INF("MSP has data already ready. Triggering data_ready_cb manually once.");
        gpio_interrupt_handler(data_ready.port, &data_ready_cb, BIT(data_ready.pin));
    }

    while (1)
    {
        k_sleep(K_MSEC(1000));
    }

    return 0;
}

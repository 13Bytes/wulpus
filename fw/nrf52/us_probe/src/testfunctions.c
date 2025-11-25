#include "testfunctions.h"
#include "main.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(main, CONFIG_LOG_DEFAULT_LEVEL);

void send_random_data(uint8_t tx_rx_id, uint16_t meas_frame_nr, struct k_msgq *ble_tx_msgq)
{
    ble_data_t tx_item = {0};
    tx_item.len = BLE_PCKT_SEND_SIZE;

    /* first chunk [0xFF, tx_rx_id, acq_nr_L, acq_nr_H, data...] */
    tx_item.data[0] = 0xFF;
    tx_item.data[1] = tx_rx_id;
    tx_item.data[2] = (uint8_t)(meas_frame_nr & 0xFF);
    tx_item.data[3] = (uint8_t)(meas_frame_nr >> 8);

    uint8_t queue_used = BLE_TX_QUEUE_SIZE - k_msgq_num_free_get(ble_tx_msgq);
    LOG_INF("BLE queue depth used: %d/%d", queue_used, BLE_TX_QUEUE_SIZE);

    int qerr = k_msgq_put(ble_tx_msgq, &tx_item, K_MSEC(10));
    if (qerr != 0)
    {
        LOG_WRN("BLE TX queue full; dropping full frame (err %d)", qerr);
    }
}

void rand_sender_thread(void *msgq_ptr, void *unused1, void *unused2)
{
    // Cast the void pointer back to k_msgq pointer
    struct k_msgq *ble_tx_msgq = (struct k_msgq *)msgq_ptr;

    uint16_t meas_frame_nr = 0;

    static uint32_t send_count = 0;
    static uint64_t current_ms = 0;

    int64_t loop_start_ms = k_uptime_get();      /* start time for the loop (ms) */
    int const FREQUENCY = 10;                    /* Hz */
    int const LOG_INTERVAL_CNT = FREQUENCY * 10; /* log every N messages */

    while (1)
    {
        /* Only send when there is an active connection */
        if (current_conn != NULL)
        {
            int64_t run_start_ms = k_uptime_get();

            send_random_data(0x00, meas_frame_nr++, ble_tx_msgq);
            send_count++;

            if ((send_count % LOG_INTERVAL_CNT) == 0)
            {
                current_ms = k_uptime_get();
                uint64_t total_elapsed_ms = current_ms - loop_start_ms;

                const uint32_t bytes_per_call = 805;
                const uint32_t total_bytes = bytes_per_call * LOG_INTERVAL_CNT;

                /* bytes per second = total_bytes * 1000 / total_elapsed_ms */
                uint32_t bytes_per_sec = (uint32_t)(total_bytes * 1000ULL / total_elapsed_ms);
                uint32_t kbps = bytes_per_sec / 1000U; /* approximate kilobytes/sec */
                int framerate = LOG_INTERVAL_CNT * 1000U / total_elapsed_ms;
                LOG_INF("send_random_data: %u ms elapsed; %u B/s (%u kB/s) - %u F/s",
                        (uint32_t)total_elapsed_ms, bytes_per_sec, kbps, framerate);
                loop_start_ms = k_uptime_get();
            }

            int64_t delay_ms = (1000 / FREQUENCY) - (k_uptime_get() - run_start_ms); /* FREQUENCY minus time the loop took */
            if (delay_ms < 0)
            {
                delay_ms = 0;
                LOG_WRN("rand_sender_thread: loop took too long; skipping sleep");
            }
            else
            {
                k_sleep(K_MSEC(delay_ms));
            }
        }
        else
        {
            /* No connection; wait a bit before retrying */
            k_sleep(K_MSEC(100));
        }
    }
}
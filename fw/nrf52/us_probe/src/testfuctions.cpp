#include "testfunctions.h"
#include "main.h"

#include <zephyr/logging/log.h>

int send_random_data(const uint8_t tx_rx_id, const uint16_t meas_frame_nr)
{
    // first chunk sends 202 bytes, the other 3 chunks are 201 bytes
    // -> total 805 bytes of data
    uint8_t buf[BYTES_PR_XFER_TX + 1];
    for (unsigned frame = 0; frame < 4; frame++)
    {
        int bt_send_ret = 0;
        // // bt_rand is too slow : (
        // if (bt_rand(buf, sizeof(buf)) != 0)
        // {
        // 	LOG_WRN("bt_rand failed");
        // 	return -1;
        // }
        if (frame == 0)
        {
            /* first chunk [0xFF, tx_rx_id, acq_nr_L, acq_nr_H, data...] */
            buf[0] = 0xFF;
            buf[1] = tx_rx_id;
            buf[2] = (uint8_t)(meas_frame_nr & 0xFF);
            buf[3] = (uint8_t)(meas_frame_nr >> 8);

            bt_send_ret = bt_nus_send(NULL, buf, sizeof(buf));
        }
        else
        {
            bt_send_ret = bt_nus_send(NULL, buf, sizeof(buf) - 1);
        }
        if (bt_send_ret != 0)
        {
            LOG_WRN("bt_nus_send failed (err %d)", bt_send_ret);
            return -1;
        }
    }
    return 0;
}

static void rand_sender_thread(void)
{
    uint16_t meas_frame_nr = 0;

    static uint32_t send_count = 0;
    static uint64_t current_ms = 0;

    /* start time for the loop (ms) */
    int64_t loop_start_ms = k_uptime_get();
    int const LOG_INTERVAL_CNT = 1000;

    for (;;)
    {
        /* Only send when there is an active connection */
        if (current_conn != NULL)
        {
            int64_t run_start_ms = k_uptime_get();

            if (send_random_data(0x00, meas_frame_nr++) == 0)
            {
                send_count++;
            }

            if ((send_count % LOG_INTERVAL_CNT) == 0)
            {
                current_ms = k_uptime_get();
                uint64_t total_elapsed_ms = current_ms - loop_start_ms;

                const uint64_t bytes_per_call = 805ULL;
                const uint64_t total_bytes = bytes_per_call * LOG_INTERVAL_CNT;

                /* bytes per second = total_bytes * 1000 / total_elapsed_ms */
                uint32_t bytes_per_sec = (uint32_t)(total_bytes * 1000ULL / total_elapsed_ms);
                uint32_t kbps = bytes_per_sec / 1000U; /* approximate kilobytes/sec */

                int framerate = LOG_INTERVAL_CNT * 1000U / total_elapsed_ms;

                LOG_INF("send_random_data: %u ms elapsed; %u B/s (%u kB/s) - %u F/s",
                        (uint32_t)total_elapsed_ms, bytes_per_sec, kbps, framerate);
                loop_start_ms = k_uptime_get();
            }

            int64_t delay_ms = (1000 / 50) - (k_uptime_get() - run_start_ms); /* 50 Hz minus time the loop took */
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
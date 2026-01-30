#include "testfunctions.h"
#include "ble.h"
#include "frame.h"
#include "main.h"
#include "tx_stats.h"
#include "uplink.h"
#include "wulpus_node.h"
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(main);

static atomic_t mesh_job_active = ATOMIC_INIT(0);
uint32_t mesh_rand_sender_period_ms;

void set_mesh_job_state(bool active)
{
    atomic_set(&mesh_job_active, active ? 1 : 0);
    LOG_INF("Sensor mock-data job state set to: %s", active ? "ACTIVE" : "INACTIVE");
}

/**
 * Sends mock data frames either to BLE TX queue (if gateway) or to Mesh TX queue
 * at a fixed interval defined by the latest incoming configuration.
 */
void mock_sender_thread(void)
{
    static uint32_t send_count = 0;
    static uint64_t current_ms = 0;

    int64_t loop_start_ms = k_uptime_get();
    mesh_rand_sender_period_ms = 2000;
    int const LOG_INTERVAL_CNT = 10;
    uint16_t frame_nr = 0;
    while (1)
    {
        if (atomic_get(&mesh_job_active))
        {
            int64_t run_start_ms = k_uptime_get();

            // Create a random frame chunk
            frame_chunk tx_chunk;
            tx_chunk.header.timestamp = wulpus_timestamp_ms_get();
            tx_chunk.header.size = BLE_PCKT_SEND_SIZE;
            tx_chunk.header.addr = wulpus_node_id_get();

            // Fill data
            for (int i = 0; i < BLE_PCKT_SEND_SIZE; i += 2)
            {
                uint16_t sample = frame_nr % 2000;
                tx_chunk.data[i] = (uint8_t)(sample & 0xFF);
                tx_chunk.data[i + 1] = (uint8_t)((sample >> 8));
            }
            tx_chunk.data[0] = 0xFF;
            tx_chunk.data[1] = 0;
            tx_chunk.data[2] = (uint8_t)(frame_nr & 0xFF);
            tx_chunk.data[3] = (uint8_t)(frame_nr >> 8);

            LOG_INF("mesh_rand_sender: generating frame nr %d", frame_nr);
            uplink_target target;
            if (uplink_enqueue_frame(&tx_chunk, K_NO_WAIT, &target) != 0)
            {
                if (target == UPLINK_TARGET_BLE)
                {
                    LOG_WRN("BLE TX queue full; dropping frame");
                    tx_stats_ble_frame_dropped_queue_full();
                }
                else
                {
                    LOG_WRN("Mesh TX queue full; dropping frame");
                    tx_stats_mesh_frame_dropped_queue_full();
                }
            }
            send_count++;
            frame_nr++;

            if ((send_count % LOG_INTERVAL_CNT) == 0)
            {
                current_ms = k_uptime_get();
                uint64_t total_elapsed_ms = current_ms - loop_start_ms;
                LOG_INF("mesh_rand_sender: %d frames sent in %u ms", LOG_INTERVAL_CNT,
                        (uint32_t)total_elapsed_ms);
                loop_start_ms = k_uptime_get();
            }

            int64_t delay_ms =
                mesh_rand_sender_period_ms - (k_uptime_get() - run_start_ms);
            if (delay_ms > 0)
            {
                k_sleep(K_MSEC(delay_ms));
            }
        }
        else
        {
            k_sleep(K_MSEC(100));
            frame_nr = 0;
        }
    }
}
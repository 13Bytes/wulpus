#ifndef TX_STATS_H
#define TX_STATS_H

#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C"
{
#endif

    struct tx_stats_snapshot
    {
        uint32_t attempted;
        uint32_t completed;
        uint32_t failed;
        uint32_t dropped_queue_full;
    };

    typedef struct tx_stats_snapshot tx_stats_snapshot;

    void tx_stats_ble_frame_attempted(void);
    void tx_stats_ble_frame_completed(void);
    void tx_stats_ble_frame_failed(void);
    void tx_stats_ble_frame_dropped_queue_full(void);

    tx_stats_snapshot tx_stats_ble_snapshot(void);

    tx_stats_snapshot tx_stats_mesh_snapshot(void);

    void tx_stats_mesh_frame_attempted(void);
    void tx_stats_mesh_frame_completed(void);
    void tx_stats_mesh_frame_failed(void);
    void tx_stats_mesh_frame_dropped_queue_full(void);

    void tx_stats_log_if_due(int64_t now_ms);
    void tx_stats_log_force(void);

#ifdef __cplusplus
}
#endif

#endif // TX_STATS_H

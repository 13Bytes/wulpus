#include "tx_stats.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(tx_stats);

#define TX_STATS_LOG_INTERVAL_MS (30 * 1000)

static atomic_t ble_attempted_frames;
static atomic_t ble_completed_frames;
static atomic_t ble_failed_frames;
static atomic_t ble_dropped_queue_full_frames;

static atomic_t mesh_attempted_frames;
static atomic_t mesh_completed_frames;
static atomic_t mesh_failed_frames;
static atomic_t mesh_dropped_queue_full_frames;

static int64_t last_log_ms;
K_MUTEX_DEFINE(tx_stats_log_mutex);

static tx_stats_snapshot snapshot(const atomic_t *attempted,
                                  const atomic_t *completed,
                                  const atomic_t *failed,
                                  const atomic_t *dropped_queue_full)
{
    return (tx_stats_snapshot){
        .attempted = (uint32_t)atomic_get(attempted),
        .completed = (uint32_t)atomic_get(completed),
        .failed = (uint32_t)atomic_get(failed),
        .dropped_queue_full = (uint32_t)atomic_get(dropped_queue_full),
    };
}

void tx_stats_ble_frame_attempted(void) { atomic_inc(&ble_attempted_frames); }
void tx_stats_ble_frame_completed(void) { atomic_inc(&ble_completed_frames); }
void tx_stats_ble_frame_failed(void) { atomic_inc(&ble_failed_frames); }
void tx_stats_ble_frame_dropped_queue_full(void)
{
    atomic_inc(&ble_dropped_queue_full_frames);
}

tx_stats_snapshot tx_stats_ble_snapshot(void)
{
    return snapshot(&ble_attempted_frames, &ble_completed_frames, &ble_failed_frames,
                    &ble_dropped_queue_full_frames);
}

void tx_stats_mesh_frame_attempted(void) { atomic_inc(&mesh_attempted_frames); }
void tx_stats_mesh_frame_completed(void) { atomic_inc(&mesh_completed_frames); }
void tx_stats_mesh_frame_failed(void) { atomic_inc(&mesh_failed_frames); }
void tx_stats_mesh_frame_dropped_queue_full(void)
{
    atomic_inc(&mesh_dropped_queue_full_frames);
}

tx_stats_snapshot tx_stats_mesh_snapshot(void)
{
    return snapshot(&mesh_attempted_frames, &mesh_completed_frames,
                    &mesh_failed_frames, &mesh_dropped_queue_full_frames);
}

static void log_snapshot(void)
{
    tx_stats_snapshot ble = tx_stats_ble_snapshot();
    tx_stats_snapshot mesh = tx_stats_mesh_snapshot();

    LOG_WRN(
        "TX stats (measurement frames) | BLE: ok=%u fail=%u dropQ=%u attempt=%u | Mesh: ok=%u fail=%u dropQ=%u attempt=%u",
        ble.completed, ble.failed, ble.dropped_queue_full, ble.attempted,
        mesh.completed, mesh.failed, mesh.dropped_queue_full, mesh.attempted);
}

void tx_stats_log_if_due(int64_t now_ms)
{
    if (k_mutex_lock(&tx_stats_log_mutex, K_NO_WAIT) != 0)
    {
        return;
    }

    if (last_log_ms == 0 || (now_ms - last_log_ms) >= TX_STATS_LOG_INTERVAL_MS)
    {
        last_log_ms = now_ms;
        log_snapshot();
    }

    k_mutex_unlock(&tx_stats_log_mutex);
}

void tx_stats_log_force(void)
{
    if (k_mutex_lock(&tx_stats_log_mutex, K_FOREVER) != 0)
    {
        return;
    }

    last_log_ms = k_uptime_get();
    log_snapshot();

    k_mutex_unlock(&tx_stats_log_mutex);
}

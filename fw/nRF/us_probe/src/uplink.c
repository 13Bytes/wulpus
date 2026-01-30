#include "uplink.h"

#include "ble.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_BT_MESH)
#include "mesh.h"
#endif

static uplink_target uplink_choose_target(void)
{
#if IS_ENABLED(CONFIG_BT_MESH)
    return i_am_gateway ? UPLINK_TARGET_BLE : UPLINK_TARGET_MESH;
#else
    return UPLINK_TARGET_BLE;
#endif
}

int uplink_enqueue_frame(const frame_chunk *chunk, k_timeout_t timeout,
                         uplink_target *target)
{
    uplink_target const selected = uplink_choose_target();
    if (target != NULL)
    {
        *target = selected;
    }

    if (selected == UPLINK_TARGET_BLE)
    {
        return k_msgq_put(&ble_tx_msgq, chunk, timeout);
    }

#if IS_ENABLED(CONFIG_BT_MESH)
    return k_msgq_put(&mesh_tx_msgq, chunk, timeout);
#else
    return -ENOTSUP;
#endif
}

uint8_t uplink_queue_depth_used(uplink_target target)
{
    if (target == UPLINK_TARGET_BLE)
    {
        return (uint8_t)(BLE_TX_QUEUE_SIZE - k_msgq_num_free_get(&ble_tx_msgq));
    }

#if IS_ENABLED(CONFIG_BT_MESH)
    return (uint8_t)(MESH_TX_QUEUE_SIZE - k_msgq_num_free_get(&mesh_tx_msgq));
#else
    return 0;
#endif
}

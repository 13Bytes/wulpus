#ifndef UPLINK_H
#define UPLINK_H

#include "frame.h"

#include <zephyr/kernel.h>

enum uplink_target
{
    UPLINK_TARGET_BLE = 0,
    UPLINK_TARGET_MESH = 1,
};

typedef enum uplink_target uplink_target;

int uplink_enqueue_frame(const frame_chunk *chunk, k_timeout_t timeout,
                         uplink_target *target);
uint8_t uplink_queue_depth_used(uplink_target target);

#endif // UPLINK_H

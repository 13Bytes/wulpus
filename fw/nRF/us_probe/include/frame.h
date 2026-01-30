#ifndef WULPUS_FRAME_H
#define WULPUS_FRAME_H

#include "ble.h"

#include <stdint.h>
#include <zephyr/toolchain.h>

struct frame_header
{
    uint32_t timestamp; // Timestamp (ms)
    uint16_t size;      // Count of bytes
    uint16_t addr;      // Sender node id
} __packed;
typedef struct frame_header frame_header;

struct frame_chunk
{
    frame_header header;
    uint8_t data[BLE_PCKT_SEND_SIZE];
} __packed;
typedef struct frame_chunk frame_chunk;

struct config_frame
{
    uint8_t data[BLE_SINGLE_PCKT_SIZE];
} __packed;
typedef struct config_frame config_frame;

#endif // WULPUS_FRAME_H

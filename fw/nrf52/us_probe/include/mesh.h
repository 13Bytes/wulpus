#ifndef MESH_H
#define MESH_H

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>
#include "ble.h"

// --- Definitions ---
#define BT_MESH_VND_MODEL_ID_WULPUS 0x0001
#define BT_MESH_VND_OP_WULPUS_FRAMECHUNK BT_MESH_MODEL_OP_3(0x52, BT_COMP_ID_LF)
#define BT_MESH_VND_OP_WULPUS_START_CONFIG BT_MESH_MODEL_OP_3(0x53, BT_COMP_ID_LF)

#define MESH_TX_QUEUE_SIZE 5

// --- Types ---
struct frame_chunk_header
{
  uint32_t timestamp; // Sequence ID (Microseconds)
  uint8_t offset;     // Chunk Index
  uint8_t size;       // Count of uint16 elements
} __packed;
typedef struct frame_chunk_header frame_chunk_header;

struct frame_chunk
{
  frame_chunk_header header;
  uint8_t data[BLE_PCKT_SEND_SIZE];
} __packed;
typedef struct frame_chunk frame_chunk;

struct config_frame
{
  uint8_t data[BLE_SINGLE_PCKT_SIZE];
} __packed;
typedef struct config_frame config_frame;

// --- Externs ---
extern struct k_msgq mesh_tx_msgq;
extern struct bt_mesh_model *vnd_model;
extern struct k_mutex mesh_pub_mutex;

// --- Functions ---
extern const struct bt_mesh_prov prov;
void mesh_tx_thread(void);
int mesh_publish_config(const uint8_t *config_data, size_t len);
extern const struct bt_mesh_comp comp;
extern uint8_t dev_uuid[16];

#endif // MESH_H

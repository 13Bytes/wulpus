#ifndef MESH_H
#define MESH_H

#include "frame.h"
#include <stdint.h>
#include <bluetooth/mesh/models.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>

// --- Definitions ---
#define BT_MESH_VND_MODEL_ID_WULPUS                                            \
  0x0001 // One custom model with multiple opcodes (functionality)
#define BT_MESH_VND_ID BT_COMP_ID_LF
#define BT_MESH_VND_OP_WULPUS_FRAMECHUNK                                       \
  BT_MESH_MODEL_OP_3(0xC2, BT_MESH_VND_ID)
#define BT_MESH_VND_OP_WULPUS_START_CONFIG                                     \
  BT_MESH_MODEL_OP_3(0xC3, BT_MESH_VND_ID)
#define BT_MESH_VND_OP_WULPUS_GATEWAY_UPDATE                                   \
  BT_MESH_MODEL_OP_3(0xC4, BT_MESH_VND_ID)
#define BT_MESH_VND_OP_WULPUS_GATEWAY_REQ                                      \
  BT_MESH_MODEL_OP_3(0xC5, BT_MESH_VND_ID)

#define MESH_TX_QUEUE_SIZE 3
#define WULPUS_GROUP_ADDR 0xC000

// --- DEBUG ---
static const uint8_t net_key[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};
static const uint8_t dev_key[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};
static const uint8_t app_key[16] = {
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};

// --- Types ---
struct frame_chunk_header {
  uint32_t timestamp; // Sequence ID (Microseconds)
  uint8_t offset;     // Chunk Index (1/2 x index of 16-byte blocks) -> bytes = offset * 4
  uint8_t size;       // Count of uint16 elements  -> bytes = size * 2
} __packed;
typedef struct frame_chunk_header frame_chunk_header;

// --- Externs ---
extern struct k_msgq mesh_tx_msgq;
extern struct bt_mesh_model *vnd_model;
extern struct k_mutex mesh_pub_mutex;
extern bool i_am_gateway;

// --- Functions ---
extern const struct bt_mesh_prov prov;
void mesh_tx_thread(void);
int mesh_publish_self_gateway();
int mesh_send_gateway_addr(uint16_t addr);
int mesh_publish_config(const uint8_t *config_data, size_t len);
int mesh_publish_config(const uint8_t *config_data, size_t len);
void mesh_request_gateway_addr(void);
void mesh_set_time_authority(void);
void mesh_unset_time_authority(void);
uint32_t mesh_get_network_timestamp(void);
extern const struct bt_mesh_comp comp;

#endif // MESH_H

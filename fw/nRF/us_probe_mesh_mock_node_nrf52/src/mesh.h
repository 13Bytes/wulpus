#ifndef MESH_H
#define MESH_H

#include <zephyr/bluetooth/mesh.h>
#include <zephyr/kernel.h>

// --- Definitions ---
#define BT_MESH_VND_MODEL_ID_WULPUS \
  0x0001 // One custom model with multiple opcodes (functionality)
#define BT_MESH_VND_ID BT_COMP_ID_LF
#define BT_MESH_VND_OP_WULPUS_FRAMECHUNK \
  BT_MESH_MODEL_OP_3(0xC2, BT_MESH_VND_ID)
#define BT_MESH_VND_OP_WULPUS_START_CONFIG \
  BT_MESH_MODEL_OP_3(0xC3, BT_MESH_VND_ID)
#define BT_MESH_VND_OP_WULPUS_GATEWAY_UPDATE \
  BT_MESH_MODEL_OP_3(0xC4, BT_MESH_VND_ID)
#define BT_MESH_VND_OP_WULPUS_GATEWAY_REQ \
  BT_MESH_MODEL_OP_3(0xC5, BT_MESH_VND_ID)

#define WULPUS_GROUP_ADDR 0xC000

#define BLE_SINGLE_PCKT_SIZE 200
#define BLE_PCKT_SEND_SIZE (201 * 4)

// --- Types ---
struct frame_chunk_header
{
  uint32_t timestamp;
  uint8_t offset;
  uint8_t size;
} __packed;
typedef struct frame_chunk_header frame_chunk_header;

struct frame_chunk
{
  frame_chunk_header header;
  uint8_t data[BLE_PCKT_SEND_SIZE];
} __packed;

// --- Functions ---
void mesh_init(void);
void mesh_start(void);
int mesh_publish_config(const uint8_t *config_data, size_t len);
void mesh_request_gateway_addr(void);
int mesh_provision_self(uint16_t addr);

// --- Keys ---
static const uint8_t net_key[16] = {
    0x01,
    0x23,
    0x45,
    0x67,
    0x89,
    0xab,
    0xcd,
    0xef,
    0x01,
    0x23,
    0x45,
    0x67,
    0x89,
    0xab,
    0xcd,
    0xef,
};
static const uint8_t dev_key[16] = {
    0x01,
    0x23,
    0x45,
    0x67,
    0x89,
    0xab,
    0xcd,
    0xef,
    0x01,
    0x23,
    0x45,
    0x67,
    0x89,
    0xab,
    0xcd,
    0xef,
};
static const uint8_t app_key[16] = {
    0x01,
    0x23,
    0x45,
    0x67,
    0x89,
    0xab,
    0xcd,
    0xef,
    0x01,
    0x23,
    0x45,
    0x67,
    0x89,
    0xab,
    0xcd,
    0xef,
};

#endif // MESH_H

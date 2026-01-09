#ifndef BLE_H
#define BLE_H

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

// --- Types ---
struct __packed ble_msg_header
{
    uint32_t timestamp; // Sequence ID (Microseconds)
    uint16_t length;    // Chunk Index
    uint16_t addr;      // Sensor address (mesh node address)
};
typedef struct ble_msg_header ble_msg_header;
#define BLE_HEADER_SZE sizeof(ble_msg_header)
#define BLE_MAX_PAYLOAD_SIZE 800

// --- Definitions ---
#define BLE_SINGLE_PCKT_SIZE 200
#define BLE_PCKT_SEND_SIZE (201 * 4) // BYTES_PR_XFER_RX * CHUNKS_PER_FRAME
#define BLE_TX_QUEUE_SIZE 5

// --- Externs ---
extern struct k_msgq ble_tx_msgq;
extern struct bt_conn *current_conn;
extern const struct gpio_dt_spec ble_cnfg_ready;

// --- Functions ---
void ble_init_device_name(uint16_t addr);
int start_advertise(void);
void ble_tx_thread(void);

// Callbacks
extern struct bt_conn_cb conn_callbacks;
extern struct bt_nus_cb nus_callbacks;

#endif // BLE_H

#ifndef BLE_H
#define BLE_H

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>


// --- Definitions ---
#define BLE_SINGLE_PCKT_SIZE 200
#define BLE_PCKT_SEND_SIZE (201 * 4) // BYTES_PR_XFER_RX * CHUNKS_PER_FRAME
#define BLE_TX_QUEUE_SIZE 5

// --- Types ---
struct ble_data_t {
  uint8_t data[BLE_PCKT_SEND_SIZE];
  uint16_t len;
};
typedef struct ble_data_t ble_data_t;

// --- Externs ---
extern struct k_msgq ble_tx_msgq;
extern struct bt_conn *current_conn;
extern const struct gpio_dt_spec ble_cnfg_ready;

// --- Functions ---
int start_advertise(void);
void ble_tx_thread(void);

// Callbacks
extern struct bt_conn_cb conn_callbacks;
extern struct bt_nus_cb nus_callbacks;

#endif // BLE_H

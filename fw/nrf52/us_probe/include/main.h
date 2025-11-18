#ifndef MAIN_H
#define MAIN_H

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <stdint.h>

// --- GPIO Definitions -------------------------
#define LED_NODE DT_ALIAS(led0)
#define DATA_READY_NODE DT_ALIAS(data_ready)
#define BLE_CNFG_READY_NODE DT_ALIAS(ble_cnfg_ready)

// --- SPIM Definitions -------------------------
#define SPIM_INST_IDX 1
#define SPI_1_PRIO 1
#define BYTES_PR_XFER_RX 201
#define BYTES_PR_XFER_TX 201
#define SPI_NODE DT_NODELABEL(spi1)
#define SPI_PINCTRL_NODE DT_CHILD(DT_PINCTRL_0(SPI_NODE, 0), group1)
#define MOSI_PIN (DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 0) & 0x3F)
#define MISO_PIN (DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 1) & 0x3F)
#define SCK_PIN (DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 2) & 0x3F)
#define SS_PIN 15
#define CHUNKS_PER_FRAME 4
#define MIN_INTERRUPT_INTERVAL_MS 15
#define TRANSFER_INTERVAL_US 300 // Time between SPI transfers (300µs as in old firmware)

// --- Bluetooth Definitions --------------------
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define BLE_PCKT_SEND_SIZE (BYTES_PR_XFER_RX * CHUNKS_PER_FRAME)

// --- Thread Priorities ------------------------
#define BLE_TASK_PRIO 1
#define SPI_TASK_PRIO 2

// --- Global GPIO Specs ------------------------
extern const struct gpio_dt_spec ble_cnfg_ready;
extern const struct gpio_dt_spec led;
extern const struct gpio_dt_spec data_ready;

// --- BLE Data Structure -----------------------
struct ble_data_t
{
    uint8_t data[BLE_PCKT_SEND_SIZE];
    uint16_t len;
};
#define BLE_TX_QUEUE_SIZE 1
extern struct bt_conn *current_conn;

// --- Function Declarations --------------------
int start_advertise(void);

#endif // MAIN_H

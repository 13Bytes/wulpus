#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

// --- GPIO Definitions -------------------------
#define LED_NODE DT_ALIAS(led0)
#define DATA_READY_NODE DT_ALIAS(data_ready)
#define BLE_CNFG_READY_NODE DT_ALIAS(ble_cnfg_ready)
#define DEBUG_LED_NODE DT_ALIAS(debug_led)

// --- SPIM Definitions -------------------------
#define SPI_NODE DT_ALIAS(spi_conn)

#if DT_NODE_EXISTS(DT_NODELABEL(spi1))
#define SPIM_INST_IDX 1
#define SS_PIN 15
#elif DT_NODE_EXISTS(DT_NODELABEL(spi20))
#define SPIM_INST_IDX 20
#define SS_PIN NRF_GPIO_PIN_MAP(2, 5)
#else
#error "No compatible SPI instance found (spi1 or spi20)"
#endif

#define SPI_1_PRIO 1
#define TRANSFER_INTERVAL_US \
  300 // Time between SPI transfers (300µs as in old firmware)

// --- Bluetooth Definitions --------------------
#define DEVICE_NAME_BASE "WULPUS_PROBE"
#define DEVICE_NAME_MAX_LEN 30
extern char device_name[DEVICE_NAME_MAX_LEN];
extern uint8_t device_name_len;

// --- Thread Priorities ------------------------
#define MESH_TX_TASK_PRIO 3
#define BLE_TASK_PRIO 2
#define SPI_TASK_PRIO 1

// --- Global GPIO Specs ------------------------
extern const struct gpio_dt_spec ble_cnfg_ready;
extern const struct gpio_dt_spec led;
extern const struct gpio_dt_spec data_ready;
extern int64_t last_gpio_interrupt_time;

#endif // MAIN_H

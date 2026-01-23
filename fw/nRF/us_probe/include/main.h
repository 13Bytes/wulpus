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
#define DBG_BUTTON_2_NODE DT_NODELABEL(dbg_button_2)
#define DBG_BUTTON_3_NODE DT_NODELABEL(dbg_button_3)

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

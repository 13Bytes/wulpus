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
#define BYTES_PR_XFER_RX 201
#define BYTES_PR_XFER_TX 201
#define SPI_PINCTRL_NODE DT_CHILD(DT_PINCTRL_0(SPI_NODE, 0), group1)

// Extract port (bits 5-9) and pin (bits 0-4) from NRF_PSEL format
#define NRF_GET_PORT(psel) (((psel) >> 5) & 0x1F)
#define NRF_GET_PIN(psel) ((psel) & 0x1F)
#define PSEL_TO_PIN(psel) NRF_GPIO_PIN_MAP(NRF_GET_PORT(psel), NRF_GET_PIN(psel))

#define MOSI_PIN PSEL_TO_PIN(DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 0))
#define MISO_PIN PSEL_TO_PIN(DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 1))
#define SCK_PIN PSEL_TO_PIN(DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 2))
#define CHUNKS_PER_FRAME 4
#define MIN_INTERRUPT_INTERVAL_MS 15
#define TRANSFER_INTERVAL_US                                                   \
  300 // Time between SPI transfers (300µs as in old firmware)

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

// --- BLE Mesh ---------------------------------
#define BT_MESH_VND_MODEL_ID_WULPUS 0x0001
#define BT_MESH_VND_OP_WULPUS_FRAMECHUNK BT_MESH_MODEL_OP_3(0x52, BT_COMP_ID_LF)

struct frame_chunk_header
{
  uint32_t timestamp; // Sequence ID (Microseconds)
  uint8_t offset;     // The first element in the data-current is the (offset * 2)nth element of the uint16 array
  uint8_t size;       // Count of how many uint16 elements are in this chunk (size * 16 / 8 = number of bytes)
} __packed;
typedef struct frame_chunk_header frame_chunk_header;

// --- BLE --------------------------------------
struct frame_data
{
  uint8_t data[BLE_PCKT_SEND_SIZE]; // Actual data
  uint16_t len;                     // Length of data in bytes
} __packed;
typedef struct frame_data frame_data;

struct ble_data_t {
  uint8_t data[BLE_PCKT_SEND_SIZE];
  uint16_t len;
};
typedef struct ble_data_t ble_data_t;

#define BLE_TX_QUEUE_SIZE 5
extern struct bt_conn *current_conn;

// --- Function Declarations --------------------
int start_advertise(void);

#endif // MAIN_H

#ifndef SPI_H
#define SPI_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

// --- Definitions ---
#define BYTES_PR_XFER_RX 210
#define BYTES_PR_XFER_TX 210
#define CHUNKS_PER_FRAME 4
#define MIN_INTERRUPT_INTERVAL_MS 15

#define SPI_PINCTRL_NODE DT_CHILD(DT_PINCTRL_0(SPI_NODE, 0), group1)

// Extract port (bits 5-9) and pin (bits 0-4) from NRF_PSEL format
#define NRF_GET_PORT(psel) (((psel) >> 5) & 0x1F)
#define NRF_GET_PIN(psel) ((psel) & 0x1F)
#define PSEL_TO_PIN(psel) NRF_GPIO_PIN_MAP(NRF_GET_PORT(psel), NRF_GET_PIN(psel))

#define MOSI_PIN PSEL_TO_PIN(DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 0))
#define MISO_PIN PSEL_TO_PIN(DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 1))
#define SCK_PIN PSEL_TO_PIN(DT_PROP_BY_IDX(SPI_PINCTRL_NODE, psels, 2))

// --- Externs ---
extern uint8_t m_tx_buffer[BYTES_PR_XFER_TX * CHUNKS_PER_FRAME];
extern uint8_t m_rx_buffer[BYTES_PR_XFER_RX * CHUNKS_PER_FRAME];

extern struct k_sem data_ready_trigger_sem;
extern struct k_sem single_session;
extern struct k_mutex tx_buffer_mutex;

// --- Functions ---
void us_spi_init(void);
void spi_session_thread(void);

#endif // SPI_H

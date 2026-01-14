#include "spi.h"
#include "ble.h"
#include "main.h"
#include "mesh.h"
#include "tx_stats.h"
#include <hal/nrf_spim.h>
#include <nrfx_spim.h>
#include <zephyr/bluetooth/mesh.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi);

// --- Globals ---
uint8_t m_tx_buffer[BYTES_PR_XFER_TX * CHUNKS_PER_FRAME] = {0};
uint8_t m_rx_buffer[BYTES_PR_XFER_RX * CHUNKS_PER_FRAME] = {0};

K_MUTEX_DEFINE(tx_buffer_mutex); // Defined here, used by BLE/Mesh

static const nrfx_spim_t spim_inst = NRFX_SPIM_INSTANCE(SPIM_INST_IDX);

// Semaphore to serialize sessions triggered by the data-ready IRQ
K_SEM_DEFINE(single_session, 1, 1);
// Semaphore signaled on SPI transfer completion
static K_SEM_DEFINE(spi_done_sem, 0, CHUNKS_PER_FRAME);
// Semaphore to trigger SPI session thread from GPIO interrupt
K_SEM_DEFINE(data_ready_trigger_sem, 0, 1);

// --- Functions ---

static void spim_handler(nrfx_spim_evt_t const *p_event, void *p_context) {
  if (p_event->type == NRFX_SPIM_EVENT_DONE) {
    // Signal that transfer is complete
    k_sem_give(&spi_done_sem);
  }
}

void us_spi_init(void) {
  LOG_INF("Starting SPI initialization");
  nrfx_err_t err;

  // Initialize SPIM
  nrfx_spim_config_t spim_config =
      NRFX_SPIM_DEFAULT_CONFIG(SCK_PIN, MOSI_PIN, MISO_PIN, SS_PIN);
  spim_config.frequency = NRFX_MHZ_TO_HZ(8);
  spim_config.mode = NRF_SPIM_MODE_1;
  spim_config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;
  spim_config.irq_priority = SPI_1_PRIO;
  err = nrfx_spim_init(&spim_inst, &spim_config, spim_handler, NULL);
  if (err != NRFX_SUCCESS) {
    LOG_ERR("Failed to initialize SPIM instance %d with err: %d", SPIM_INST_IDX,
            err);
    return;
  }

  IRQ_DIRECT_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_SPIM_INST_GET(SPIM_INST_IDX)),
                     IRQ_PRIO_LOWEST, NRFX_SPIM_INST_HANDLER_GET(SPIM_INST_IDX),
                     0);
  irq_enable(NRFX_IRQ_NUMBER_GET(NRF_SPIM_INST_GET(SPIM_INST_IDX)));

  LOG_INF("SPI init complete");
}

void spi_session_thread(void) {
  LOG_INF("SPI session thread spawned");
  uint16_t my_addr = bt_mesh_primary_addr();

  while (1) {
    // Wait for GPIO interrupt to trigger a session
    LOG_INF("Waiting for SPI trigger...");
    k_sem_take(&data_ready_trigger_sem, K_FOREVER);
    LOG_INF("SPI session thread activated");

    // Ensure only one read session runs at a time (serialize sessions)
    if (k_sem_take(&single_session, K_NO_WAIT) != 0) {
      LOG_WRN("SPI session already in progress; skipping trigger");
      continue;
    }

    LOG_INF("Starting SPI session - single 804-byte transfer");

    // Single transfer for all 804 bytes (4 * 201)
    // Hardware limitation is 255 bytes per transfer, so we still need multiple
    // calls BUT we set up the descriptor once per session
    nrfx_spim_xfer_desc_t xfer = NRFX_SPIM_XFER_TRX(
        m_tx_buffer, BYTES_PR_XFER_TX, m_rx_buffer, BYTES_PR_XFER_RX);

    bool xfer_failed = false;
    for (int i = 0; i < CHUNKS_PER_FRAME; i++) {
      // Update pointers for this chunk (manual increment since HW has 255-byte
      // limit)
      xfer.p_tx_buffer = &m_tx_buffer[i * BYTES_PR_XFER_TX];
      xfer.p_rx_buffer = &m_rx_buffer[i * BYTES_PR_XFER_RX];

      nrfx_err_t nerr =
          nrfx_spim_xfer(&spim_inst, &xfer, NRFX_SPIM_FLAG_REPEATED_XFER);
      if (nerr != NRFX_SUCCESS) {
        LOG_ERR("SPI xfer %d start failed: %d", i, nerr);
        xfer_failed = true;
        break;
      }

      // Wait for this chunk to complete
      if (k_sem_take(&spi_done_sem, K_MSEC(100)) != 0) {
        LOG_ERR("SPI xfer %d timeout", i);
        xfer_failed = true;
        break;
      }
    }

    if (!xfer_failed) {
      LOG_INF("SPI session complete");
      LOG_INF("SPI RX: SOF=0x%02X, tx_rx_id=%d, frame_nr=%d", m_rx_buffer[0],
              m_rx_buffer[1], (m_rx_buffer[3] << 8) | m_rx_buffer[2]);

      bool all_zero = true;
      for (int i = 4; i < 100; i++) {
        if (m_rx_buffer[i] != 0) {
          all_zero = false;
          break;
        }
      }
      if (all_zero) {
        LOG_WRN("Frame appears to be empty/stopped - NOT enqueueing");
        k_sem_give(&single_session);
        continue; // Don't enqueue empty frames
      }

      // Build one full frame and enqueue as a single item
      frame_chunk chunk = {0};
      chunk.header.timestamp = mesh_get_network_timestamp();
      chunk.header.size = BLE_PCKT_SEND_SIZE;
      chunk.header.addr = my_addr;
      memcpy(&chunk.data, m_rx_buffer, BLE_PCKT_SEND_SIZE);

      uint8_t queue_used =
          BLE_TX_QUEUE_SIZE - k_msgq_num_free_get(&mesh_tx_msgq);
      LOG_INF("Mesh queue depth used: %d/%d", queue_used, MESH_TX_QUEUE_SIZE);

      int qerr = k_msgq_put(&mesh_tx_msgq, &chunk, K_MSEC(10));
      if (qerr != 0) {
        LOG_WRN("Mesh TX queue full; dropping full frame (err %d)", qerr);
        tx_stats_mesh_frame_dropped_queue_full();
      }
    }
    // Release session gate
    k_sem_give(&single_session);
  }
}

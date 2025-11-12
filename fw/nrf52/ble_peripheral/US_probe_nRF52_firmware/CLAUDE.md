# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an nRF52832 Zephyr RTOS firmware that acts as a BLE gateway for an ultrasound acquisition system. The nRF52 communicates with an MSP430 microcontroller (in a separate `wulpus_msp430_firmware` repository) via SPI and streams ultrasound measurement data over Bluetooth Low Energy using Nordic UART Service (NUS).

**Key Architecture:**

- nRF52: BLE gateway + SPI master
- MSP430: Ultrasound acquisition controller + SPI device
- Data flow: BLE ↔ nRF52 ↔ SPI ↔ MSP430

## Build Commands

This project uses the Nordic nRF Connect SDK (v3.1.1) with Zephyr RTOS.

**Build:**

```bash
# Build for nRF52DK board (nrf52832)
west build -b nrf52dk/nrf52832

# Clean build
west build -b nrf52dk/nrf52832 -p
```

**Flash:**

```bash
west flash
```

**View logs (RTT):**

```bash
# Logs are output via SEGGER RTT
# Use JLinkRTTViewer or JLinkRTTClient to view logs
```

## Important Hardware Configuration

**Board:** nrf52dk/nrf52832

**GPIO Pins (defined in app.overlay):**

- LED: P0.10 (led0)
- BLE Config Ready (output to MSP): P0.19 (ble-cnfg-ready)
- Data Ready (input from MSP): P0.20 (data-ready, with pull-down)

**SPI1 Master (8 MHz, Mode 1):**

- MOSI: P0.14
- MISO: P0.13
- SCK: P0.16
- CS: P0.15 (hardcoded)

## Code Architecture

### Communication Protocol

**SPI Session Structure:**

- 4 chunks per session, 201 bytes per chunk = 804 bytes total
- Full-duplex transfers using `nrfx_spim` with `NRFX_SPIM_FLAG_REPEATED_XFER`
- Triggered by rising edge on `data_ready` GPIO from MSP430

**Frame Format:**

- Byte 0: Start of Frame (0xFF)
- Byte 1: tx_rx_id (TX/RX config index)
- Bytes 2-3: meas_frame_nr (little-endian frame number)
- Bytes 4-803: Measurement data

**BLE Transmission:**

- Device name: "WULPUS_PROBE_3" (configurable in prj.conf)
- MTU: 220 bytes (L2CAP_TX_MTU)
- First BLE chunk: 202 bytes (includes SOF marker)
- Subsequent chunks: 201 bytes each
- Uses Nordic UART Service (NUS) for data streaming

### Threading Model

1. **Main thread:** Initializes peripherals, then sleeps
2. **System workqueue:** Handles `data_ready_work_handler` (SPI transfers)
   - Stack size: 2048 bytes (CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE)
3. **BLE TX thread:** Priority 3, 2048 byte stack
   - Dequeues from `ble_tx_msgq` (15 slots, 4-byte aligned)
   - Sends frames over BLE NUS with retry logic

### Synchronization

**Semaphores:**

- `spi_done_sem`: Signaled by SPIM ISR on transfer completion
- `single_session`: Serializes SPI sessions (prevents overlapping reads)

**Message Queue:**

- `ble_tx_msgq`: 15 slots of 804-byte frames for BLE transmission

### Key State Machine

1. **Initialization:** 5s delay → GPIO setup → BLE advertising → SPI init
2. **Config Reception (BLE → MSP):**
   - BLE receives config via `bt_received()` → copies to `m_tx_buffer`
   - Sets `ble_cnfg_ready` high to notify MSP
   - MSP triggers `data_ready` to request SPI session
   - nRF clocks out config via MOSI
3. **Measurement Streaming (MSP → BLE):**
   - MSP completes acquisition → triggers `data_ready`
   - nRF runs 4-chunk SPI session → receives 804 bytes into `m_rx_buffer`
   - Enqueues to `ble_tx_msgq`
   - BLE thread sends 4 chunks over NUS

## Configuration

**Key Zephyr Config (prj.conf):**

- `CONFIG_BT_DEVICE_NAME`: Change BLE advertising name
- `CONFIG_BT_L2CAP_TX_MTU=220`: BLE MTU size
- `CONFIG_LOG_DEFAULT_LEVEL=3`: Log level (3=INFO)
- `CONFIG_NRFX_SPIM1=y`: Enable SPI1 master
- `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048`: Adjust if SPI handler needs more stack

**BLE Connection Parameters (optimized for throughput):**

- Connection interval: 6-12 units (7.5-15ms)
- Latency: 0
- Timeout: 400 units (4s)
- Preferred PHY: 2M

## Critical Implementation Details

### Timing Constraints

- SPI transfer timeout: 100ms per chunk
- BLE send retries: 10 attempts with 10ms backoff for `-ENOBUFS`/`-EAGAIN`
- GPIO interrupt logs time deltas (see Strange-behaviour.txt for known issue: every third interrupt appears skipped)

### Error Handling

- If any SPI chunk times out or fails, entire session is aborted
- Failed BLE sends drop the frame after retries exhausted
- On BLE disconnect, `ble_cnfg_ready` is set low

### Known Issues

- GPIO interrupt timing anomaly: 1000ms, 2000ms, 1000ms, 2000ms pattern
- First BLE chunk is 202 bytes (includes extra byte for SOF signaling); this is safe as the last byte will be ignored by the recipient

## Related Codebases

**MSP430 Firmware:** Located at `wulpus_msp430_firmware/` (sibling directory, separate repository)

- Handles ultrasound acquisition (USS library, SAPH/USS, HV MUX)
- Acts as SPI device
- See findings.md for detailed protocol documentation

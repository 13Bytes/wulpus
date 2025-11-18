# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an nRF52832 Zephyr RTOS firmware that acts as a BLE gateway for an ultrasound acquisition system. The nRF52 communicates with an MSP430 microcontroller (in a separate folder at `../../msp430/`) via SPI and streams ultrasound measurement data over Bluetooth Low Energy using Nordic UART Service (NUS).

**Code Structure:**

- `src/main.cpp`: Main firmware implementation
- `src/main.h`: Shared definitions, constants, and thread priorities
- `src/testfunctions.cpp/h`: Test utilities for BLE throughput testing

**Key Architecture:**

- nRF52: BLE gateway + SPI master
- MSP430: Ultrasound acquisition controller + SPI device
- Data flow: BLE ↔ nRF52 ↔ SPI ↔ MSP430

## Build Commands

This project uses the Nordic nRF Connect SDK (v3.1.1) with Zephyr RTOS.

**Build:**

The build requires a enviornament with a few added environment variables.
You need to add those once before executing the commands:

```ps
$env:PATH = "C:\ncs\toolchains\c1a76fddb2;C:\ncs\toolchains\c1a76fddb2\mingw64\bin;C:\ncs\toolchains\c1a76fddb2\bin;C:\ncs\toolchains\c1a76fddb2\opt\bin;C:\ncs\toolchains\c1a76fddb2\opt\bin\Scripts;C:\ncs\toolchains\c1a76fddb2\opt/nanopb/generator-bin;C:\ncs\toolchains\c1a76fddb2\nrfutil\bin;C:\ncs\toolchains\c1a76fddb2\opt\zephyr-sdk\arm-zephyr-eabi\bin;C:\ncs\toolchains\c1a76fddb2\opt\zephyr-sdk\riscv64-zephyr-elf\bin;$env:PATH" ; $env:PYTHONPATH = "C:\ncs\toolchains\c1a76fddb2\opt\bin;C:\ncs\toolchains\c1a76fddb2\opt\bin\Lib;C:\ncs\toolchains\c1a76fddb2\opt\bin\Lib\site-packages" ; $env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr" ; $env:ZEPHYR_SDK_INSTALL_DIR = "C:\ncs\toolchains\c1a76fddb2\opt\zephyr-sdk" ; $env:ZEPHYR_BASE = "C:\ncs\v3.1.1\zephyr";
```

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
Logs are output via SEGGER RTT
Use JLinkRTTViewer or JLinkRTTClient to view logs

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
- First BLE chunk: 202 bytes (the last byte will not be read, it is just used to indicate a new frame throuh beeing a longer frame)
- Subsequent chunks: 201 bytes each
- Uses Nordic UART Service (NUS) for data streaming

### Threading Model

1. **Main thread:** Initializes peripherals, then sleeps
2. **SPI session thread:** Priority 2 (SPI_TASK_PRIO), 2048 byte stack
   - Dedicated thread for SPI transfers (defined in `src/main.h`)
   - Triggered by `data_ready_trigger_sem` semaphore from GPIO interrupt
   - Runs `spi_session_thread()` which executes 4-chunk SPI sessions (804 bytes total)
   - Enqueues received data to `ble_tx_msgq`
3. **BLE TX thread:** Priority 1 (BLE_TASK_PRIO), 2048 byte stack
   - Dequeues from `ble_tx_msgq` (15 slots, 4-byte aligned)
   - Runs `ble_tx_thread()` which sends frames over BLE NUS with retry logic

**Note:** In Zephyr, **lower priority numbers = higher priority**. SPI thread (priority 2) can preempt BLE thread (priority 1) to ensure timely SPI transfers.

### Synchronization

**Semaphores:**

- `spi_done_sem`: Signaled by SPIM ISR on transfer completion
- `single_session`: Serializes SPI sessions (prevents overlapping reads)
- `data_ready_trigger_sem`: Signals SPI session thread from GPIO interrupt handler
- `ble_tx_ready_sem`: Signals when BLE is ready to transmit (unused in current implementation)

**Message Queue:**

- `ble_tx_msgq`: BLE_TX_QUEUE_SIZE slots of 804-byte frames for BLE transmission (defined globally)

**Mutex:**

- `tx_buffer_mutex`: Protects `m_tx_buffer` during BLE config reception

### Key State Machine

1. **Initialization:** 5s delay → GPIO setup → BLE advertising → SPI init
2. **Config Reception (BLE → MSP):**
   - BLE receives config via `bt_received()` → copies to `m_tx_buffer`
   - Sets `ble_cnfg_ready` high to notify MSP
   - MSP triggers `data_ready` to request SPI session
   - nRF clocks out config via MOSI
3. **Measurement Streaming (MSP → BLE):**
   - MSP completes acquisition → triggers `data_ready` GPIO (rising edge)
   - GPIO interrupt handler (`gpio_interrupt_handler()`) signals `data_ready_trigger_sem`
   - SPI session thread wakes up and runs 4-chunk SPI session → receives 804 bytes into `m_rx_buffer`
   - SPI thread validates data (checks for non-zero content) and enqueues to `ble_tx_msgq`
   - BLE thread dequeues and sends 4 chunks over NUS with retry logic

## Configuration

**Key Zephyr Config (prj.conf):**

- `CONFIG_BT_DEVICE_NAME`: Change BLE advertising name (e.g., "WULPUS_PROBE_3")
- `CONFIG_BT_L2CAP_TX_MTU=220`: BLE MTU size
- `CONFIG_LOG_DEFAULT_LEVEL=3`: Log level (3=INFO)
- `CONFIG_NRFX_SPIM1=y`: Enable SPI1 master
- Thread priorities and stack sizes are defined in `src/main.h`

**BLE Connection Parameters (optimized for throughput):**

- Connection interval: 6-12 units (7.5-15ms)
- Latency: 0
- Timeout: 400 units (4s)
- Preferred PHY: 2M

## Critical Implementation Details

### Timing Constraints

- SPI transfer timeout: 100ms per chunk (in `spi_session_thread()`)
- BLE send retries: 10 attempts with 50µs sleep for errors, `k_yield()` for backpressure
- GPIO interrupt debouncing: Minimum 15ms interval (`MIN_INTERRUPT_INTERVAL_MS`)
- GPIO interrupt logs time deltas (see Strange-behaviour.txt for known issue: every third interrupt appears skipped)

### Error Handling

- If any SPI chunk times out or fails, entire session is aborted (session semaphore released)
- Empty frames (all zeros in first 100 bytes) are not enqueued to BLE queue
- Failed BLE sends drop the frame after retries exhausted (logged as warning)
- On BLE disconnect, `ble_cnfg_ready` is set low in `disconnected()` callback

### Performance Optimizations

- **Dedicated SPI Thread:** SPI transfers run on a dedicated high-priority thread (priority 2) instead of the system workqueue, preventing BLE stack operations from blocking SPI sessions
- **Direct Semaphore Signaling:** GPIO interrupt uses direct semaphore signaling (`k_sem_give()`) instead of workqueue submission for lower latency
- **Frame Validation:** Empty frames are detected and dropped before enqueueing to save BLE bandwidth

## Related Codebases

**MSP430 Firmware:** Located at `msp430` (sibling directory)

- Handles ultrasound acquisition (USS library, SAPH/USS, HV MUX)
- Acts as SPI device
- See findings.md for detailed protocol documentation

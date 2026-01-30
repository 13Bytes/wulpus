# WULPUS Agent Guidelines

## Commands

- **Start backend**: `sw/.venv/Scripts/Activate.ps1` then `python -m wulpus.main` (hosts backend at http://127.0.0.1:8000/)
- **Install backend deps (alternative to existing venv)**: `pip install -r sw/wulpus/requirements.txt`
- **Frontend dev**: `npm run dev` in `sw/wulpus-frontend/` (dev server at http://localhost:5173/)
- **Frontend install**: `npm i` in `sw/wulpus-frontend/`
- **Frontend build**: `python build.py` in `sw/wulpus-frontend/` (builds and copies into `sw/wulpus/production-frontend/` for the backend to serve in production)
- **Frontend lint**: `npm run lint` in `sw/wulpus-frontend/`
- **Build nRF firmware**: `C:\ncs\v3.1.1\zephyr\zephyr-env.cmd` to get access to all zephyr commands. Then `west build` and optional `west flash`

## Architecture

- **Firmware**: `fw/msp430/` (ultrasound MCU), `fw/nRF/` (BLE MCU + optional USB dongle) with `fw/nRF/us_probe` beeing the the most relevant
- **Firmware**: `fw/msp430/` (ultrasound MCU), `fw/nRF/` (BLE MCU + optional USB dongle) with `fw/nRF/us_probe` being the most relevant
- **Software**: `sw/wulpus/` (FastAPI backend), `sw/wulpus-frontend/` (React frontend)
- **Communication between Backend and Frontend**: WebSocket for real-time data, REST API for control-messages

## Codebase Overview

### Repo map

- **Firmware**
	- `fw/nRF/us_probe/`: Zephyr app running on the probe's nRF52/nRF53 class SoC. Bridges MSP430 data via SPI and exposes BLE (NUS) + optional BLE Mesh.
	- `fw/msp430/`: MSP430 ultrasound acquisition firmware (source project in `fw/msp430/wulpus_msp430_firmware/`).
	- `fw/nRF/dongle/`: optional/legacy USB dongle firmware.
- **Software**
	- `sw/wulpus/`: FastAPI backend that manages device connections, configuration, acquisition/series loops, processing, and serves the built frontend.
	- `sw/wulpus-frontend/`: React + Vite frontend UI.
### Key data flow (end-to-end)

- **UI → Backend (REST)**: connect/disconnect, start/stop acquisition, start/stop series, manage logs/configs, update analysis config.
- **Backend → UI (WebSocket)**: live status updates and processed measurement frames.
- **Probe firmware**
	- **MSP430 → nRF**: frames over SPI, triggered by a data-ready GPIO interrupt.
	- **nRF → host**: BLE Nordic UART Service (NUS) for config/control in and frame data out.
	- **nRF ↔ nRF (optional)**: BLE Mesh vendor messages for broadcasting config and forwarding measurement frames; a gateway node can forward into BLE.

### Where to look (common tasks)

- **Backend entrypoint / server**: `sw/wulpus/main.py` (`python -m wulpus.main`)
	- REST endpoints live in `sw/wulpus/main.py` under `/api/*`
	- WebSocket endpoint is `/ws`
	- Built frontend is served from `sw/wulpus/production-frontend/`
- **Backend config encoding (bytes over BLE)**: `sw/wulpus/wulpus_api.py` (e.g. `gen_conf_package()`)
- **Device I/O implementations**:
	- Selection/abstraction: `sw/wulpus/interface.py`
	- Direct BLE (Bleak): `sw/wulpus/interface_direct.py`
	- USB dongle: `sw/wulpus/interface_usb.py`
	- Mock: `sw/wulpus/interface_mock.py`
- **WebSocket broadcast + processing**: `sw/wulpus/websocket_manager.py` + `sw/wulpus/data_processing.py`
- **Series measurements**: `sw/wulpus/series.py`
- **Saved artifacts**:
	- measurements: `sw/wulpus/measurements/` (download via `/logs/...`)
	- configs: `sw/wulpus/configs/`

### Firmware (nRF us_probe) orientation

- App entrypoint and thread startup: `fw/nRF/us_probe/src/main.c`
- BLE (NUS) transport: `fw/nRF/us_probe/src/ble.c`
- BLE Mesh transport and vendor messaging: `fw/nRF/us_probe/src/mesh.c`
- SPI session handling and MSP430 framing: `fw/nRF/us_probe/src/spi.c`

### Frontend orientation

- Vite app: `sw/wulpus-frontend/`
- Production build integration: `sw/wulpus-frontend/build.py` copies build output into `sw/wulpus/production-frontend/` (used by the backend to serve the UI)

## Code Style

- **Python**: Type hints, snake_case, async/await for I/O
- **TypeScript/React**: camelCase, functional components, strict typing
- **Imports**: Standard library first, third-party, then local imports
- **File structure**: Clear separation of concerns, API models in separate files
- **Error handling**: Use proper exception types, HTTP status codes for API errors
- **Data processing**: NumPy for measurements, Pandas for structured data analysis

### General
Write code where the naming of variables and functions is self explanatory.
Comments should be used sparingly.
Make sure to keep try-catch sections to a minimum. Especially don't catch error-cases that logically can't occur.
import asyncio
import inspect
import json
import os
import time
from typing import List, Optional

import uvicorn
from fastapi import (FastAPI, File, HTTPException, UploadFile, WebSocket,
                     WebSocketDisconnect, Request)
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
from helper import check_if_filereq_is_legitimate, ensure_dir

import wulpus as wulpus_pkg
from wulpus.websocket_manager import WebsocketManager
from wulpus.wulpus import Wulpus
from wulpus.wulpus_config_models import ComPort, TxRxConfig, UsConfig, WulpusConfig
from wulpus.wulpus_mock import WulpusMock

MEASUREMENTS_DIR = os.path.join(os.path.dirname(
    inspect.getfile(wulpus_pkg)), 'measurements')
CONFIG_DIR = os.path.join(os.path.dirname(
    inspect.getfile(wulpus_pkg)), 'configs')
FRONTEND_DIR = os.path.join(os.path.dirname(
    inspect.getfile(wulpus_pkg)), 'production-frontend')

wulpus = Wulpus()
wulpus_mock = WulpusMock()

manager = WebsocketManager(wulpus)
app = FastAPI()
global_send_data_task = None


@app.post("/api/start")
async def start(config: WulpusConfig):
    try:
        manager.get_wulpus().connect()
    except ValueError as e:
        return {"connection-error": str(e)}
    manager.get_wulpus().set_config(config)
    await manager.get_wulpus().start()
    return {"ok": "ok"}


@app.post("/api/stop")
def stop():
    manager.get_wulpus().stop()
    return {"ok": "ok"}


@app.get("/api/connections")
def get_connections():
    return manager.get_wulpus().get_connection_options()


@app.post("/api/connect")
def connect(conf: ComPort):
    manager.get_wulpus().connect(conf.com_port)


@app.post("/api/disconnect")
def disconnect():
    manager.get_wulpus().disconnect()


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    global global_send_data_task
    await manager.connect(websocket)
    asyncio.create_task(manager.send_status(websocket))
    if global_send_data_task is None or global_send_data_task.done():
        new_measurement_event = asyncio.Event()

        wulpus.set_new_measurement_event(new_measurement_event)
        wulpus_mock.set_new_measurement_event(new_measurement_event)

        global_send_data_task = asyncio.create_task(
            manager.send_data(new_measurement_event))

    latest_frame = manager.get_wulpus().get_latest_frame()
    if latest_frame is not None:
        await manager.broadcast_json(latest_frame)
    try:
        while True:
            data = await websocket.receive_text()
            await manager.broadcast_text(f"Client says: {data}")
    except WebSocketDisconnect:
        manager.disconnect(websocket)
        await manager.broadcast_text("A Client left the chat")


@app.get("/api/logs", response_model=List[str])
def list_logs() -> List[str]:
    """Return list of saved measurement files (npz) relative names."""
    ensure_dir(MEASUREMENTS_DIR)
    try:
        files = [f for f in os.listdir(
            MEASUREMENTS_DIR) if f.lower().endswith('.npz')]
        files.sort(reverse=True)
        return files
    except FileNotFoundError:
        return []


@app.get("/api/logs/{filename}")
def download_log(filename: str):
    """Download a specific measurement file by filename."""
    ensure_dir(MEASUREMENTS_DIR)
    filepath = check_if_filereq_is_legitimate(
        filename, MEASUREMENTS_DIR, '.npz')
    return FileResponse(filepath, media_type='application/octet-stream', filename=filename)


@app.get("/api/configs", response_model=List[str])
def list_configs() -> List[str]:
    """Return list of saved config files (json) relative names."""
    ensure_dir(CONFIG_DIR)
    try:
        files = [f for f in os.listdir(
            CONFIG_DIR) if f.lower().endswith('.json')]
        files.sort(reverse=True)
        return files
    except FileNotFoundError:
        return []


@app.get("/api/configs/{filename}")
def download_config(filename: str):
    """Download a specific config file by filename."""
    ensure_dir(CONFIG_DIR)
    filepath = check_if_filereq_is_legitimate(
        filename, CONFIG_DIR, '.json')
    return FileResponse(filepath, media_type='application/octet-stream', filename=filename)


@app.post("/api/configs")
async def save_config(config: WulpusConfig, name: Optional[str] = None):
    """Save the provided config JSON to a file in the configs directory."""
    ensure_dir(CONFIG_DIR)
    # derive safe base filename
    if name is None or len(name.strip()) == 0:
        name = "wulpus-config-" + \
            time.strftime("%Y-%m-%d_%H-%M-%S", time.localtime())
    # very simple sanitization
    if os.path.sep in name or (os.path.altsep and os.path.altsep in name) or len(name) > 100:
        raise HTTPException(status_code=400, detail="Invalid name")
    base = os.path.join(CONFIG_DIR, name)
    filename = base + ".json"
    # avoid overwriting existing files
    suffix = 1
    while os.path.exists(filename):
        filename = f"{base}_{suffix}.json"
        suffix += 1
    try:
        with open(filename, 'w', encoding='utf-8') as f:
            json.dump(config.model_dump(), f, ensure_ascii=False, indent=2)
        return {"filename": os.path.basename(filename)}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e)) from e


@app.delete("/api/configs/{filename}")
def delete_config(filename: str):
    """Delete a specific config file by filename."""
    ensure_dir(CONFIG_DIR)
    try:
        filepath = check_if_filereq_is_legitimate(
            filename, CONFIG_DIR, '.json')
        os.remove(filepath)
        return {"ok": True}
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e)) from e


@app.post("/api/activate-mock")
def activate_mock():
    wulpus.stop()
    manager.set_wulpus(wulpus_mock)
    return {"ok": "ok"}


@app.post("/api/deactivate-mock")
def deactivate_mock():
    wulpus_mock.stop()
    manager.set_wulpus(wulpus)
    return {"ok": "ok"}


@app.post("/api/replay/{filename}")
async def replay_file(filename: str):
    # Build a minimal default config: one empty TxRxConfig and a UsConfig with its own defaults
    default_config = WulpusConfig(
        tx_rx_config=[TxRxConfig()], us_config=UsConfig())
    wulpus.stop()
    ensure_dir(MEASUREMENTS_DIR)
    manager.set_wulpus(wulpus_mock)
    filepath = check_if_filereq_is_legitimate(
        filename, MEASUREMENTS_DIR, '.npz')
    wulpus_mock.set_config(default_config)
    wulpus_mock.set_replay_file(filepath)
    await wulpus_mock.start()

app.mount("/assets", StaticFiles(directory=os.path.join(FRONTEND_DIR,
          'assets')), name="assets")


@app.get("/{full_path:path}")
async def frontend_fallback(full_path: str, request: Request):
    index_path = os.path.join(FRONTEND_DIR, "index.html")
    return FileResponse(index_path)

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

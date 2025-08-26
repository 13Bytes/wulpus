import asyncio
import inspect
import os
from typing import List
import time
import json



import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, HTTPException, UploadFile, File
from typing import Optional
from fastapi.responses import FileResponse

from helper import check_if_filereq_is_legitimate, ensure_dir
import wulpus as wulpus_pkg
from wulpus.wulpus_config_models import ComPort, WulpusConfig
from wulpus.websocket_manager import WebsocketManager
from wulpus.wulpus import Wulpus

MEASUREMENTS_DIR = os.path.join(os.path.dirname(
    inspect.getfile(wulpus_pkg)), 'measurements')
CONFIG_DIR = os.path.join(os.path.dirname(
    inspect.getfile(wulpus_pkg)), 'configs')

wulpus = Wulpus()
manager = WebsocketManager(wulpus)
app = FastAPI()
global_send_data_task = None

@app.get("/")
def root():
    return ["Welcome to Wulpus - In the future you will be greeted by a beautiful UI "]


@app.post("/start")
async def start(config: WulpusConfig):
    try:
        wulpus.connect()
    except ValueError as e:
        return {"connection-error": str(e)}
    wulpus.set_config(config)
    await wulpus.start()
    return {"ok": "ok"}


@app.post("/stop")
def stop():
    wulpus.stop()
    return {"ok": "ok"}


@app.get("/connections")
def get_connections():
    return wulpus.get_connection_options()


@app.post("/connect")
def connect(conf: ComPort):
    wulpus.connect(conf.com_port)


@app.post("/disconnect")
def disconnect():
    wulpus.disconnect()


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    global global_send_data_task
    await manager.connect(websocket)
    asyncio.create_task(manager.send_status(websocket))
    if global_send_data_task is None or global_send_data_task.done():
        new_measurement_event = asyncio.Event()
        wulpus.set_new_measurement_event(new_measurement_event)
        global_send_data_task = asyncio.create_task(
            manager.send_data(new_measurement_event))
    latest_frame = wulpus.get_latest_frame()
    if latest_frame is not None:
        await manager.broadcast_json(latest_frame.tolist())
    try:
        while True:
            data = await websocket.receive_text()
            await manager.broadcast_text(f"Client says: {data}")
    except WebSocketDisconnect:
        manager.disconnect(websocket)
        await manager.broadcast_text("A Client left the chat")


@app.get("/logs", response_model=List[str])
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


@app.get("/logs/{filename}")
def download_log(filename: str):
    """Download a specific measurement file by filename."""
    ensure_dir(MEASUREMENTS_DIR)
    filepath = check_if_filereq_is_legitimate(
        filename, MEASUREMENTS_DIR, '.npz')
    return FileResponse(filepath, media_type='application/octet-stream', filename=filename)


@app.get("/configs", response_model=List[str])
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


@app.get("/configs/{filename}")
def download_config(filename: str):
    """Download a specific config file by filename."""
    ensure_dir(CONFIG_DIR)
    filepath = check_if_filereq_is_legitimate(
        filename, CONFIG_DIR, '.json')
    return FileResponse(filepath, media_type='application/octet-stream', filename=filename)


@app.post("/configs")
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


@app.delete("/configs/{filename}")
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


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

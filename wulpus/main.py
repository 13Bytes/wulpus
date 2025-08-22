import asyncio
import time

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from wulpus.wulpus_config_models import ComPort, WulpusConfig
from wulpus.websocket_manager import WebsocketManager
from wulpus.wulpus import Wulpus


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


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

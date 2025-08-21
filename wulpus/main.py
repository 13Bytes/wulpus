import asyncio

import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from wulpus.config_models import ComPort, WulpusConfig
from wulpus.websocket_manager import WebsocketManager
from wulpus.wulpus import Wulpus


wulpus = Wulpus()
manager = WebsocketManager(wulpus)
app = FastAPI()


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
    wulpus.start()
    await wulpus.listen_measurement_data(manager)
    # async for measurement in wulpus:
    #     await manager.broadcast_text("measurement")
    return {"ok": "ok"}


@app.get("/connections")
def get_connections():
    return wulpus.get_connection_options()


@app.post("/connect")
def connect(conf: ComPort):
    wulpus.connect(conf.com_port)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    asyncio.create_task(manager.send_status(websocket))
    try:
        while True:
            data = await websocket.receive_text()
            await manager.broadcast_text(f"Client says: {data}")
    except WebSocketDisconnect:
        manager.disconnect(websocket)
        await manager.broadcast_text("A Client left the chat")


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

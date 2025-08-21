from __future__ import annotations
import json
from fastapi import WebSocket
from fastapi.websockets import WebSocketState
from fastapi.encoders import jsonable_encoder
import asyncio
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from wulpus.wulpus import Wulpus


class WebsocketManager:
    def __init__(self, _wulpus: Wulpus):
        self.active_connections: list[WebSocket] = []
        self.wulpus = _wulpus

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def send_single_client(self, message: str, websocket: WebSocket):
        await websocket.send_text(message)

    async def broadcast_text(self, message: str):
        for connection in self.active_connections:
            await connection.send_text(message)

    async def broadcast_json(self, message: json):
        for connection in self.active_connections:
            await connection.send_json(message)

    async def send_status(self, websocket: WebSocket):
        try:
            while websocket.application_state == WebSocketState.CONNECTED:
                status = self.wulpus.get_status()
                await websocket.send_json(jsonable_encoder(status))
                await asyncio.sleep(3)
        except RuntimeError:  # Client disconnected
            return

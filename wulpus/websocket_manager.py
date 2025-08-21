from __future__ import annotations
import asyncio
import json
from typing import TYPE_CHECKING

import json_numpy
from fastapi import WebSocket, WebSocketDisconnect
from fastapi.encoders import jsonable_encoder
from fastapi.websockets import WebSocketState

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
            try:
                await connection.send_text(message)
            except RuntimeError:
                self.disconnect(connection)

    async def broadcast_json(self, message: json):
        for connection in self.active_connections:
            try:
                await connection.send_json(message)
            except (RuntimeError, WebSocketDisconnect):  # Client disconnected
                self.disconnect(connection)

    async def send_status(self, websocket: WebSocket):
        try:
            while websocket.application_state == WebSocketState.CONNECTED:
                status = self.wulpus.get_status()
                await websocket.send_json(jsonable_encoder(status))
                await asyncio.sleep(3)
        except (RuntimeError, WebSocketDisconnect):  # Client disconnected
            return

    async def send_data(self, new_measurement_event: asyncio.Event):
        # Send latest frame to new client
        latest_frame = self.wulpus.get_latest_frame()
        if not latest_frame is None:
            await self.broadcast_json(json_numpy.dumps(latest_frame))

        while True:
            await new_measurement_event.wait()
            new_measurement_event.clear()
            data, acq_num, tx_rx_id = await self.wulpus.get_new_measurement()
            await self.broadcast_json(json_numpy.dumps(data))

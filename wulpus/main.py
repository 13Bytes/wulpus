import asyncio
import os
import sys
import time
from contextlib import asynccontextmanager
from enum import IntEnum
from threading import Thread
from typing import Union

import numpy as np
import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.websockets import WebSocketState
from pydantic import BaseModel

from wulpus.config_models import ComPort, TxRxConfig, UsConfig, WulpusConfig
from wulpus.dongle import WulpusDongle
from wulpus.dongle_mock import WulpusDongleMock
from wulpus.wulpus_api import gen_conf_package, gen_restart_package


class Status(IntEnum):
    NOT_CONNECTED = 0
    CONNECTING = 1
    READY = 2
    RUNNING = 3
    ERROR = 9


class Wulpus:
    def __init__(self):
        self._config: Union[WulpusConfig, None] = None
        self._status: Status = Status.NOT_CONNECTED
        # self._dongle = WulpusDongle()
        self._dongle = WulpusDongleMock()
        self._last_connection: str = ''
        self._latest_frame: Union[np.ndarray, None] = None

    def get_connection_options(self):
        return self._dongle.get_available()

    def connect(self, device_name: str = ''):
        if self._status == Status.READY:
            return
        if len(device_name) == 0:
            if len(self._last_connection) > 0:
                device_name = self._last_connection
            else:
                raise ValueError("No device name specified.")

        self._last_connection = device_name
        self._status = Status.CONNECTING
        if self._dongle.open(device_str=device_name):
            self._status = Status.READY
        else:
            self._status = Status.NOT_CONNECTED

    def get_status(self):
        return {"status": self._status, "bluetooth": self._dongle.get_status(), "config": self._config}

    def set_config(self, config: WulpusConfig) -> bytes:
        self._config = config

    def start(self):
        """
        Start executing the config. Config needs to be set before starting.
        """
        if self._status == Status.RUNNING:
            return
        if not self._config:
            raise ValueError("No configuration set.")
        bytes_config = gen_conf_package(self._config)

        # Send a restart command (in case the system is already running)
        # TODO: Remove after live config-update is tested
        self._dongle.send_config(gen_restart_package())
        time.sleep(2.5)

        if self._dongle.send_config(bytes_config):
            self._status = Status.RUNNING
        else:
            self._status = Status.NOT_CONNECTED

    async def listen_measurement_data(self):
        """
        Start listening for measurement data from the dongle.
        """
        if self._status != Status.RUNNING:
            return
        await asyncio.create_task(self.__measure())

    def __measure(self):
        number_of_acq = self._config.us_config.num_acqs
        num_samples = self._config.us_config.num_samples
        data_arr = np.zeros((num_samples, number_of_acq), dtype='<i2')
        acq_num_arr = np.zeros(number_of_acq, dtype='<u2')
        tx_rx_id_arr = np.zeros(number_of_acq, dtype=np.uint8)
        # Acquisition counter
        data_cnt = 0
        acquisition_running = True
        while data_cnt < number_of_acq and acquisition_running:
            # Receive the data
            data = self._dongle.receive_data()
            if data is not None:
                self._latest_frame = data[0]
                data_arr[:, data_cnt] = data[0]
                acq_num_arr[data_cnt] = data[1]
                tx_rx_id_arr[data_cnt] = data[2]
                data_cnt += 1

        # Trim data to actual measured size
        data_arr = data_arr[:, :data_cnt]
        acq_num_arr = acq_num_arr[:data_cnt]
        tx_rx_id_arr = tx_rx_id_arr[:data_cnt]


class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def send_single_client(self, message: str, websocket: WebSocket):
        await websocket.send_text(message)

    async def broadcast(self, message: str):
        for connection in self.active_connections:
            await connection.send_text(message)

    async def send_status(self, websocket: WebSocket):
        try:
            while websocket.application_state == WebSocketState.CONNECTED:
                status = wulpus.get_status()
                await websocket.send_json(status)
                await asyncio.sleep(3)
        except RuntimeError:  # Client disconnected
            return


manager = ConnectionManager()
wulpus = Wulpus()
app = FastAPI()


@app.get("/")
def root():
    return {"message": "Hello World"}


@app.post("/start")
async def start(config: WulpusConfig):
    try:
        wulpus.connect()
    except ValueError as e:
        return {"connection-error": str(e)}
    wulpus.set_config(config)
    wulpus.start()
    await wulpus.listen_measurement_data()
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
            await manager.broadcast(f"Client says: {data}")
    except WebSocketDisconnect:
        manager.disconnect(websocket)
        await manager.broadcast("A Client left the chat")


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

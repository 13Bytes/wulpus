from __future__ import annotations

import asyncio
import inspect
import io
import os
import time
from typing import Union
from zipfile import ZipFile

import numpy as np
import pandas as pd
from wulpus.wulpus_model import Measurement, Status
from wulpus.helper import ensure_dir
from wulpus.interface import DongleInterface, ReceiveDataPayload
from wulpus.interface_direct import WulpusDongleDirect
from wulpus.interface_usb import WulpusDongleUsb
from wulpus.wulpus_api import (DATA_FILE_EXTENSION, gen_conf_package,
                               gen_restart_package)
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from wulpus.wulpus_config_models import WulpusConfig

import wulpus as wulpus_pkg


class Wulpus:
    def __init__(self, wulpus_id=0):
        self.wulpus_id: int = wulpus_id
        self._config: Union[WulpusConfig, None] = None
        self._status: Status = Status.NOT_CONNECTED
        self._interface_usb_dongle = WulpusDongleUsb()
        self._interface_direct = WulpusDongleDirect(
            disconnected_callback=self._disconnected_callback
        )
        self._last_connection: str = ''
        self._latest_frame: Union[Measurement, None] = None
        self._data:  Union[np.ndarray, None] = None
        self._mesh_origin:  Union[np.ndarray, None] = None
        self._data_acq_num:  Union[np.ndarray, None] = None
        self._data_tx_rx_id:  Union[np.ndarray, None] = None
        self._data_time:  Union[np.ndarray, None] = None
        # Event to signal new measurement data for WebSocket clients
        self._new_measurement = asyncio.Event()
        self._recording_start = time.time()
        self._live_data_cnt = 0
        self._acquisition_running = False

    def _get_current_interface(self) -> DongleInterface:
        if (self._last_connection.startswith("COM") or self._last_connection.startswith("/dev/")):
            return self._interface_usb_dongle
        else:
            return self._interface_direct

    def get_acquisition_running(self) -> bool:
        return self._acquisition_running

    def get_config(self):
        return self._config

    async def get_connection_options(self):
        conn_1 = await self._interface_direct.get_available()
        conn_2 = await self._interface_usb_dongle.get_available()
        return list(conn_1) + list(conn_2)

    async def connect(self, device_name: str = ''):
        if self._status == Status.READY:
            return
        if len(device_name) == 0:
            if len(self._last_connection) > 0:
                device_name = self._last_connection
            else:
                raise ValueError("No device name specified.")

        self._last_connection = device_name
        self._status = Status.CONNECTING

        if await self._get_current_interface().connect(device_str=device_name):
            self._status = Status.READY
            return
        self._status = Status.NOT_CONNECTED

    async def disconnect(self):
        await self._get_current_interface().close()
        self._status = Status.NOT_CONNECTED

    def get_status(self):
        return {"status": self._status,
                "bluetooth": self._get_current_interface().get_status(),
                "endpoint": self._get_current_interface().get_connection_endpoint(),
                "us_config": self._config.us_config if self._config else None,
                "tx_rx_config": self._config.tx_rx_config if self._config else None,
                "progress": self._live_data_cnt / self._config.us_config.num_acqs if self._config else 0,
                }

    def set_config(self, config: WulpusConfig) -> bytes:
        self._config = config

    async def start(self):
        """
        Start executing the config. Config needs to be set before starting.
        """
        if self._status == Status.RUNNING:
            return
        if not self._config:
            raise ValueError("No configuration set.")
        bytes_config = gen_conf_package(self._config)

        # Send a restart command (in case the system is already running)
        # Note: Can be removed after live config-update is tested
        await self._get_current_interface().send_config(gen_restart_package())
        await asyncio.sleep(2.5)

        if await self._get_current_interface().send_config(bytes_config):
            self._status = Status.RUNNING
            asyncio.create_task(self._measure())
        else:
            self._status = Status.NOT_CONNECTED

    def stop(self):
        """
        Stops measurement task by using a flag
        """
        self._acquisition_running = False

    def set_new_measurement_event(self, event: asyncio.Event):
        self._new_measurement = event

    async def _measure(self):
        self._recording_start = time.time()
        number_of_acq = self._config.us_config.num_acqs
        num_samples = self._config.us_config.num_samples
        self._data = np.zeros((num_samples, number_of_acq), dtype='<i2')
        self._mesh_origin = np.zeros(number_of_acq, dtype=np.uint32)
        self._data_acq_num = np.zeros(number_of_acq, dtype='<u2')
        self._data_tx_rx_id = np.zeros(number_of_acq, dtype=np.uint8)
        self._data_time = np.zeros(number_of_acq, dtype=np.uint64)
        # Acquisition counter
        data_cnt = 0
        self._acquisition_running = True
        current_intf = self._get_current_interface()
        while data_cnt < number_of_acq and self._acquisition_running:
            payload = await current_intf.receive_data(
                self, self._config.us_config.num_samples
            )
            if payload is None or not self._acquisition_running:
                continue

            measurement: Measurement = self._structure_measurement(payload)
            self._latest_frame = measurement
            self._data[:, data_cnt] = payload["rf_data"]
            self._mesh_origin[data_cnt] = measurement["mesh_origin"]
            self._data_acq_num[data_cnt] = payload["acq_number"]
            self._data_tx_rx_id[data_cnt] = payload["tx_rx_id"]
            self._data_time[data_cnt] = measurement["time"]
            # Signal new data; will be cleared by listener in associated `task_broadcast_data()`, which accesses `get_latest_frame()`
            self._new_measurement.set()
            data_cnt += 1
            self._live_data_cnt = data_cnt

        # stop measurement
        await current_intf.send_config(gen_restart_package())
        # Trim data to actual measured size
        self._data = self._data[:, :data_cnt]
        self._mesh_origin = self._mesh_origin[:data_cnt]
        self._data_acq_num = self._data_acq_num[:data_cnt]
        self._data_time = self._data_time[:data_cnt]
        self._data_tx_rx_id = self._data_tx_rx_id[:data_cnt]
        self._acquisition_running = False
        if (self._status == Status.RUNNING):
            self._status = Status.READY
        self._save_measurement()

    def _save_measurement(self):
        start_time = time.localtime(self._recording_start)
        timestring = time.strftime("%Y-%m-%d_%H-%M-%S", start_time)
        devicestring = self._last_connection \
            .replace('/dev/', '').replace('COM', '').replace(' ', '') \
            .replace('/', '_').replace(':', '')
        filename = "wulpus-" + timestring + f"-id-{devicestring}"
        # Ensure measurement directory exists
        module_path = os.path.dirname(inspect.getfile(wulpus_pkg))
        measurement_path = os.path.join(module_path, 'measurements')
        ensure_dir(measurement_path)
        basepath = os.path.join(measurement_path, filename)

        # Check if filename exists (.npz gets added by .savez command)
        while os.path.isfile(basepath + DATA_FILE_EXTENSION):
            basepath = basepath + "_conflict"

        if self._data.shape[1] == 0:
            print('No data recorded, measurement not saved.')
            return

        df = pd.DataFrame({
            'measurement': [pd.Series(self._data[:, i]) for i in range(self._live_data_cnt)],
            "tx": [self._config.tx_rx_config[i].tx_channels for i in self._data_tx_rx_id],
            "rx": [self._config.tx_rx_config[i].rx_channels for i in self._data_tx_rx_id],
            "wulpus": self._mesh_origin,
            "aq_number": self._data_acq_num,
            "log_version": 1,
            "tx_rx_id": self._data_tx_rx_id
        }, index=self._data_time)

        # Expand measurement series into columns so they're all included in save-format (parquet)
        measurement_expanded = pd.DataFrame(
            [m.values for m in df['measurement']], index=df.index)
        flattened_df = pd.concat(
            [df.drop(columns=['measurement']), measurement_expanded], axis=1)
        flattened_df.columns = [str(col) for col in flattened_df.columns]

        with ZipFile(basepath + DATA_FILE_EXTENSION, 'w') as zf:
            zf.writestr('config-0.json', self._config.model_dump_json())
            # Write dataframe as parquet
            buffer = io.BytesIO()
            flattened_df.to_parquet(buffer)
            zf.writestr('data.parquet', buffer.getvalue())

        print('Data saved in ' + basepath + DATA_FILE_EXTENSION)

    def get_latest_frame(self) -> Union[Measurement, None]:
        return self._latest_frame

    def _structure_measurement(self, payload: ReceiveDataPayload, **kvargs) -> Measurement:
        rf_data = payload["rf_data"]
        tx_rx_id = payload["tx_rx_id"]
        mesh_origin = payload.get("sensor_addr", 0)

        tx_rx_config = self._config.tx_rx_config[tx_rx_id]
        measurement_data = Measurement(
            data=rf_data.tolist(),
            time=payload.get("timestamp", int(time.time_ns() / 1e3)),
            tx=tx_rx_config.tx_channels if tx_rx_config.tx_channels else [],
            rx=tx_rx_config.rx_channels if tx_rx_config.rx_channels else [],
            mesh_origin=mesh_origin,
        )
        measurement_data.update(kvargs)
        return measurement_data

    def _disconnected_callback(self, *args, **kwargs):
        print("Device disconnected unexpectedly.")
        self._status = Status.NOT_CONNECTED
        self._acquisition_running = False

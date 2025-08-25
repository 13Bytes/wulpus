import asyncio
import os
import time
from enum import IntEnum
from typing import Union

import numpy as np

from wulpus.wulpus_config_models import WulpusConfig
from wulpus.dongle_mock import WulpusDongleMock
from wulpus.dongle import WulpusDongle
from wulpus.websocket_manager import WebsocketManager
from wulpus.wulpus_api import gen_conf_package, gen_restart_package
import wulpus
import inspect


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
        self._dongle = WulpusDongle()
        # self._dongle = WulpusDongleMock()
        self._last_connection: str = ''
        self._latest_frame: Union[np.ndarray, None] = None
        self._data:  Union[np.ndarray, None] = None
        self._data_acq_num:  Union[np.ndarray, None] = None
        self._data_tx_rx_id:  Union[np.ndarray, None] = None
        # Event to signal new measurement data for WebSocket clients
        self._new_measurement = asyncio.Event()
        self._recording_start = time.time()
        self._live_data_cnt = 0
        self._acquisition_running = False

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

    def disconnect(self):
        self._dongle.close()
        self._status = Status.NOT_CONNECTED

    def get_status(self):
        return {"status": self._status,
                "bluetooth": self._dongle.get_status(),
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
        # TODO: Remove after live config-update is tested
        self._dongle.send_config(gen_restart_package())
        await asyncio.sleep(2.5)

        if self._dongle.send_config(bytes_config):
            self._status = Status.RUNNING
            asyncio.create_task(self.__measure())
        else:
            self._status = Status.NOT_CONNECTED

    def stop(self):
        """
        Stops measurement task by using a flag
        """
        self._acquisition_running = False

    def set_new_measurement_event(self, event: asyncio.Event):
        self._new_measurement = event

    async def __measure(self):
        self._recording_start = time.time()
        number_of_acq = self._config.us_config.num_acqs
        num_samples = self._config.us_config.num_samples
        self._data = np.zeros((num_samples, number_of_acq), dtype='<i2')
        self._data_acq_num = np.zeros(number_of_acq, dtype='<u2')
        self._data_tx_rx_id = np.zeros(number_of_acq, dtype=np.uint8)
        # Acquisition counter
        data_cnt = 0
        self._acquisition_running = True
        while data_cnt < number_of_acq and self._acquisition_running:
            # Receive the data
            data = self._dongle.receive_data()
            if data is not None:
                self._latest_frame = data[0]
                self._data[:, data_cnt] = data[0]
                self._data_acq_num[data_cnt] = data[1]
                self._data_tx_rx_id[data_cnt] = data[2]
                self._new_measurement.set()
                data_cnt += 1
                self._live_data_cnt = data_cnt
            await asyncio.sleep(0.001)

        # stop measurement
        self._dongle.send_config(gen_restart_package())
        # Trim data to actual measured size
        self._data = self._data[:, :data_cnt]
        self._data_acq_num = self._data_acq_num[:data_cnt]
        self._data_tx_rx_id = self._data_tx_rx_id[:data_cnt]
        self._status = Status.READY
        self.__save_measurement()

    def __save_measurement(self):
        start_time = time.localtime(self._recording_start)
        timestring = time.strftime("%Y-%m-%d_%H-%M-%S", start_time)
        filename = "wulpus-data-" + timestring
        module_path = os.path.dirname(inspect.getfile(wulpus))
        measurement_path = os.path.join(module_path, 'measurements')
        os.chdir(measurement_path)

        # Check if filename exists (.npz gets added by .savez command)
        while os.path.isfile(filename + '.npz'):
            filename = filename + "_conflict"
        # Save numpy data array to file
        np.savez_compressed(filename,
                            data_arr=self._data,
                            acq_num_arr=self._data_acq_num,
                            tx_rx_id_arr=self._data_tx_rx_id,
                            record_start=np.array([self._recording_start]))

        print('Data saved in ' + filename + '.npz')

    def get_latest_frame(self):
        return self._latest_frame

    async def get_measurement(self):
        return self._data, self._data_acq_num, self._data_tx_rx_id

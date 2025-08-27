import asyncio
import inspect
import json
import os
import time
from enum import IntEnum
from typing import Union

import numpy as np

import wulpus as wulpus_pgk

from wulpus.dongle import WulpusDongle
from wulpus.wulpus import Wulpus, Status
from wulpus.dongle_mock import WulpusDongleMock
from wulpus.wulpus_api import gen_conf_package, gen_restart_package
from wulpus.wulpus_config_models import WulpusConfig


class WulpusMock(Wulpus):
    def __init__(self):
        super().__init__()
        self._dongle = WulpusDongleMock()
        self._status = Status.READY
        self._replay_file = None

    def get_status(self):
        status = super().get_status()
        status["mock"] = True
        return status

    def set_replay_file(self, file_path: Union[str, None]):
        if file_path is None:
            self._replay_file = None
            return
        elif not os.path.isfile(file_path):
            raise ValueError(f"File {file_path} does not exist.")
        print(f"Replaying file set to {file_path}")
        self._replay_file = file_path

    async def _measure(self):
        if self._replay_file is None:
            # Simulate reading random data from mocked dongle
            await super()._measure()
        else:
            # Replay file
            data = np.load(self._replay_file)
            self._status = Status.RUNNING
            self._acquisition_running = True
            self._data = data['data_arr']
            self._data_acq_num = data['acq_num_arr']
            self._data_tx_rx_id = data['tx_rx_id_arr']

            data_cnt = data['data_arr'].shape[1]
            num_samples = data['data_arr'].shape[0]

            self._config.us_config.num_acqs = data_cnt
            self._config.us_config.num_samples = num_samples

            index = 0
            while index < data_cnt and self._acquisition_running:
                await asyncio.sleep(0.1)
                # await asyncio.sleep(self._config.us_config.meas_period/1e6)
                self._latest_frame = self._data[:, index]
                self._new_measurement.set()
                index += 1
                self._live_data_cnt = index
            self._acquisition_running = False
            self.set_replay_file(None)
            self._status = Status.READY

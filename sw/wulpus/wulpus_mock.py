import asyncio
import os
from typing import Union

import numpy as np
from wulpus.interface import ReceiveDataPayload
from wulpus.interface_mock import WulpusDongleMock
from wulpus.helper import zip_to_dataframe

from wulpus.wulpus import Status, Wulpus


class WulpusMock(Wulpus):
    def __init__(self, wulpus_id=0):
        super().__init__(wulpus_id=wulpus_id)
        self._interface_usb_dongle = WulpusDongleMock()
        self._interface_direct = WulpusDongleMock()
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
            # Replay from new zip format
            self._status = Status.RUNNING
            self._acquisition_running = True

            df, config = zip_to_dataframe(self._replay_file)

            self._config = config

            self._data = np.column_stack([
                np.asarray(m, dtype=np.int16) for m in df['measurement']
            ])
            # Cast arrays to expected dtypes
            self._data_acq_num = df['aq_number'].to_numpy(dtype='<u2')
            self._data_tx_rx_id = df['tx_rx_id'].to_numpy(dtype=np.uint8)
            self._data_time = df.index.to_numpy(dtype=np.uint64)
            self._mesh_origin = df['wulpus'].to_numpy()
            # Update number of measurements with actual recorded ones
            data_cnt = self._data.shape[1]
            num_samples = self._data.shape[0]
            self._config.us_config.num_acqs = data_cnt
            self._config.us_config.num_samples = num_samples

            index = 0
            while index < data_cnt and self._acquisition_running:
                await asyncio.sleep(self._config.us_config.meas_period / 1e6)
                payload: ReceiveDataPayload = {
                    "rf_data": self._data[:, index],
                    "acq_number": int(self._data_acq_num[index]),
                    "tx_rx_id": int(self._data_tx_rx_id[index]),
                    "timestamp": int(self._data_time[index]),
                }
                sensor_addr = self._parse_sensor_addr(self._mesh_origin[index])
                if sensor_addr is not None:
                    payload["sensor_addr"] = sensor_addr

                self._latest_frame = self._structure_measurement(payload)
                self._new_measurement.set()
                index += 1
                self._live_data_cnt = index
            self._acquisition_running = False
            self.set_replay_file(None)
            self._status = Status.READY

    def _parse_sensor_addr(self, sensor_addr: object) -> Union[int, None]:
        if isinstance(sensor_addr, str):
            if sensor_addr == "default":
                return None
            return int(sensor_addr, 16)
        return int(sensor_addr)

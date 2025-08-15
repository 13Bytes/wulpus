"""
   Copyright (C) 2023 ETH Zurich. All rights reserved.
   Author: Sergei Vostrikov, ETH Zurich
           Cedric Hirschi, ETH Zurich
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

   SPDX-License-Identifier: Apache-2.0
"""

import serial
from serial.tools.list_ports import comports
from serial.tools.list_ports_common import ListPortInfo
import numpy as np
from wulpus.dongle import WulpusDongle

ACQ_LENGTH_SAMPLES = 400


class WulpusDongleMock(WulpusDongle):
    """
    Class representing the Wulpus dongle (mock implementation).
    """

    def __init__(self, port: str = '', timeout_write: int = 3, baudrate: int = 4000000):
        self.port = port
        self.timeout_write = timeout_write
        self.baudrate = baudrate

        self.acq_length = ACQ_LENGTH_SAMPLES
        self.acq_num = 0

    def get_available(self):
        """
        Get a list of available devices.
        """
        ports = comports()
        return sorted(ports)

    def open(self, device: ListPortInfo = None):
        """
        Open the device connection.
        """
        self.acq_num = 0
        return True

    def close(self):
        """
        Close the device connection.
        """
        return True

    def send_config(self, conf_bytes_pack: bytes):
        """
        Send a configuration package to the device.
        """
        print("Configuration sent:", conf_bytes_pack)
        self.acq_num = 0
        return True

    def receive_data(self):
        """
        Mock: Return random data with the same structure as the original.
        """
        rf_arr = np.random.randint(
            1, 1001, size=ACQ_LENGTH_SAMPLES, dtype="<i2")
        tx_rx_id = 0
        acq_num = self.acq_num

        self.acq_num += 1
        return rf_arr, acq_num, tx_rx_id

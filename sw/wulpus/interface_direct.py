from __future__ import annotations
import asyncio
import re
from typing import Callable, Union, Optional, List
import time

import numpy as np

from bleak import BleakClient, BleakScanner, BLEDevice
from bleak.exc import BleakError
from serial.tools.list_ports_common import ListPortInfo
from typing import TYPE_CHECKING
from .interface import (
    ConnectionOption,
    ConnectionType,
    DongleInterface,
    ReceiveDataPayload,
)
if TYPE_CHECKING:
    from wulpus.wulpus import Wulpus

# Standard Nordic UART Service (NUS) UUIDs
NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX_CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # From PC to Wulpus
NUS_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # From Wulpus to PC
WULPUS_NAME_PATTERN = re.compile(r"WULPUS_PROBE_[0-9A-Fa-f]+")


class WulpusDongleDirect(DongleInterface):
    """
    Class representing the Wulpus via direct Bluetooth connection
    """

    def __init__(self, disconnected_callback: Optional[Callable[[BleakClient], None]] = None) -> None:
        super().__init__()
        self._devices: list[BLEDevice] = []
        self._bleak_client: Union[BleakClient, None] = None
        self._data_queue: Optional["asyncio.Queue[bytes]"] = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._disconnected_callback = disconnected_callback
        self._last_frame_time: Optional[float] = None

    async def get_available(self) -> List[ConnectionOption]:
        """
        Get a list of available devices.

        Returns:
            list[dict[str, object]]: A list of dicts with keys "device", "description" and "type" (ConnectionType).
        """
        try:
            devices = await BleakScanner.discover()
            devices = [
                d for d in devices if d.name and WULPUS_NAME_PATTERN.fullmatch(d.name)]
            self._devices = sorted(devices, key=lambda d: d.name)
            return [
                {"device": str(d.address), "description": str(
                    d.name), "type": ConnectionType.BLE}
                for d in self._devices
            ]
        except OSError:
            print("OSError during Bluetooth discovery - Adapter probably disabled")
            return []

    async def connect(self, device: Optional[ListPortInfo] = None, device_str: Optional[str] = None) -> bool:
        """
        Open the device connection.
        """
        if (device):
            raise ValueError(
                "Device works only with Serial USB Dongle (not direct BLE connection)")

        target_address: Optional[str] = None
        if device_str:
            target_address = device_str
        # If a previous discovery returned devices, allow selecting by index via device.device if provided as ListPortInfo-like.
        # But for BLE we'll take device_str (MAC) as the source of truth.

        if not target_address:
            # Try to pick the first discovered WULPUS device
            if not self._devices:
                await self.get_available()
            if not self._devices:
                print("Error: no BLE WULPUS device found.")
                return False
            target = self._devices[0]
            target_address = target.address

        try:
            # Bind to the current running loop and create queue here to avoid cross-loop issues
            self._loop = asyncio.get_running_loop()
            # Reasonable buffer to absorb short bursts without unbounded growth
            self._data_queue = asyncio.Queue(maxsize=1000)
            self._bleak_client = BleakClient(
                target_address, disconnected_callback=self._disconnected_callback)
            await self._bleak_client.connect()
            await self._bleak_client.start_notify(NUS_TX_CHAR_UUID, self._notification_handler)
            return True
        except (BleakError, OSError, TimeoutError) as e:
            print("Error while trying to open BLE device:", e)
            return False

    def _notification_handler(self, _sender: int, data: bytearray):
        """
        Handle incoming notifications from the BLE device.
        """
        # Ensure we enqueue on the correct event loop thread (Bleak callback may be on another thread)
        if self._loop and self._data_queue is not None:
            def _enqueue():
                try:
                    if self._data_queue.full():
                        # Drop oldest to make space
                        self._data_queue.get_nowait()
                    self._data_queue.put_nowait(bytes(data))
                except (asyncio.QueueEmpty, asyncio.QueueFull):
                    # Swallow to avoid noisy exceptions in callback thread
                    pass
            self._loop.call_soon_threadsafe(_enqueue)

    async def close(self):
        """
        Close the device connection.
        """
        try:
            if self._bleak_client and self._bleak_client.is_connected:
                await self._bleak_client.stop_notify(NUS_TX_CHAR_UUID)
                try:
                    await self._bleak_client.disconnect()
                except BleakError:
                    pass
        except BleakError as e:
            print("Error while trying to close BLE device:", e)
        finally:
            self._bleak_client = None
            self._data_queue = None
            self._loop = None

    async def send_config(self, conf_bytes_pack: bytes):
        """
        Send a configuration package to the device.
        """
        if not self._bleak_client:
            print("Error: BLE client is not connected.")
            return False

        try:
            await self._bleak_client.write_gatt_char(NUS_RX_CHAR_UUID, conf_bytes_pack)
            return True
        except BleakError as e:
            print("Error while trying to send config to BLE device:", e)
            return False

    async def receive_data(
        self,
        wulpus: Wulpus,
        acq_length: int = 400,
    ) -> Optional[ReceiveDataPayload]:
        """
        Receives and processes data from the Wulpus device.

        The function assembles a complete data frame from incoming BLE packets.
        A frame is composed of 4 packets, totaling 804 bytes.
        - The first packet is 202 bytes, with the first byte being a start-of-frame marker (0xFF).
          The last byte of this packet is ignored.
        - The following three packets are each 201 bytes.

        The assembled 804-byte frame has the following structure:
        - 1-byte start of frame marker (0xFF)
        - 1-byte tx_rx_id
        - 2-byte acquisition number (little-endian)
        - 800-byte RF data payload (400 samples of 16-bit signed integers)
        """
        if not self._bleak_client or not self._bleak_client.is_connected:
            print("Error: BLE client is not connected.")
            return None

        if self._data_queue is None:
            print("Error: Data queue not initialized.")
            return None

        frame_buffer = bytearray()
        hdr_timestamp = 0
        hdr_body_length = 0
        hdr_addr = 0

        while wulpus.get_acquisition_running():
            try:
                # Wait for data with a timeout to allow checking the acquisition_running flag
                data = await asyncio.wait_for(self._data_queue.get(), timeout=0.1)
            except asyncio.TimeoutError:
                continue

            # Start of a new frame
            if len(data) > 100 and data[8] == 0xFF:
                if len(frame_buffer) > 0:
                    print(
                        f"Warning: Incomplete frame discarded ({len(frame_buffer)} bytes)")
                hdr_timestamp = int.from_bytes(data[0:4], 'little')
                hdr_body_length = int.from_bytes(data[4:6], 'little')
                hdr_addr = int.from_bytes(data[6:8], 'little')
                frame_buffer = bytearray(data)
                continue

            # Subsequent packets of the current frame
            if frame_buffer:
                frame_buffer.extend(data)

            # A full frame has been received
            if hdr_body_length != 0 and len(frame_buffer) == hdr_body_length:
                # Frame structure: [0xFF, tx_rx_id, acq_nr_L, acq_nr_H, data...]
                tx_rx_id = frame_buffer[9]
                acq_nr = int.from_bytes(frame_buffer[10:12], 'little')
                # The actual RF data
                rf_arr = np.frombuffer(frame_buffer[12:], dtype='<i2')

                if len(rf_arr) == acq_length:
                    now = time.perf_counter()
                    if self._last_frame_time is not None:
                        dt_ms = (now - self._last_frame_time) * 1000.0
                        dt_str = f"{dt_ms:.1f} ms"
                    else:
                        dt_str = "n/a"
                    self._last_frame_time = now
                    if acq_nr % 50 == 0:
                        print(
                            f"state of frames: acq_nr={acq_nr}, tx_rx_id={tx_rx_id}, dt={dt_str} (to prev. one)")
                        print(
                            f"Frame header: addr {hdr_addr}, timestamp {hdr_timestamp}")
                    return {
                        "rf_data": rf_arr,
                        "acq_number": int(acq_nr),
                        "tx_rx_id": int(tx_rx_id),
                        "sensor_addr": int(hdr_addr),
                        "timestamp": int(hdr_timestamp),
                    }
                else:
                    print(
                        f"Warning: Malformed frame received (data length {len(rf_arr)}, expected {acq_length}). Discarding.")
                    # Reset buffer if data is malformed
                    frame_buffer = bytearray()

            elif hdr_body_length != 0 and len(frame_buffer) > hdr_body_length:
                print(
                    f"Warning: Oversized frame discarded ({len(frame_buffer)} bytes received, expected {hdr_body_length}).")
                frame_buffer = bytearray()

        return None

    def get_status(self):
        if self._bleak_client and self._bleak_client.is_connected:
            addr = getattr(self._bleak_client, 'address', None)
            return f"connected to {addr}" if addr else "connected"
        return "not connected"

    def get_connection_endpoint(self) -> str:
        if self._bleak_client and self._bleak_client.is_connected:
            addr = getattr(self._bleak_client, 'address', None)
            return addr if addr else ""
        return ""


if __name__ == "__main__":
    async def _main():
        dongle = WulpusDongleDirect()
        print(await dongle.get_available())
    asyncio.run(_main())

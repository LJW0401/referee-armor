"""Unit tests for handshake and status correlation without physical hardware."""

from __future__ import annotations

import unittest

from armor_host.client import ArmorClient, ConnectionError
from armor_host.protocol import decode_frame, encode_frame


class FakeSerial:
    """In-memory serial peer that replies to the two currently defined commands."""

    def __init__(self, wrong_nonce: bool = False) -> None:
        self._incoming = bytearray()
        self.wrong_nonce = wrong_nonce
        self.closed = False

    def read(self, size: int = 1) -> bytes:
        if not self._incoming:
            return b""
        result = bytes(self._incoming[:size])
        del self._incoming[:size]
        return result

    def write(self, data: bytes) -> int:
        request = decode_frame(data[:-1])
        if request.frame_type == 0x01:
            nonce = int.from_bytes(request.payload, "little")
            if self.wrong_nonce:
                nonce += 1
            payload = (
                nonce.to_bytes(4, "little")
                + (0x112233445566).to_bytes(8, "little")
                + bytes((1, 1, 2, 3))
                + (3).to_bytes(4, "little")
            )
            self._incoming.extend(encode_frame(0x81, request.sequence, payload))
        elif request.frame_type == 0x02:
            payload = (
                bytes((1,))
                + (0x0013).to_bytes(2, "little")
                + (1000).to_bytes(4, "little")
                + (12345).to_bytes(4, "little", signed=True)
                + (15).to_bytes(4, "little")
                + bytes((4, 2, 12, 34, 56, 0, 0, 0))
            )
            self._incoming.extend(encode_frame(0x82, request.sequence, payload))
        elif request.frame_type == 0x10:
            self._incoming.extend(encode_frame(0x90, request.sequence))
        return len(data)

    def reset_input_buffer(self) -> None:
        self._incoming.clear()

    def close(self) -> None:
        self.closed = True


class ArmorClientTests(unittest.TestCase):
    """Verify device identity correlation and decoded status values."""

    def test_connects_then_reads_status(self) -> None:
        connection = FakeSerial()
        client = ArmorClient(connection)
        device = client.connect()
        status = client.get_status()
        self.assertEqual(device.device_id, "0000112233445566")
        self.assertEqual(device.firmware_version, "1.2.3")
        self.assertEqual(device.capabilities, 3)
        self.assertEqual(status.weight_mg, 12345)
        self.assertEqual(status.sample_age_ms, 15)
        self.assertEqual(status.led_count, 4)
        self.assertEqual((status.led_red, status.led_green, status.led_blue), (12, 34, 56))

    def test_sets_one_color_for_both_light_strips(self) -> None:
        client = ArmorClient(FakeSerial())
        client.connect()
        client.set_led_color(0, 0, 255)

    def test_rejects_out_of_range_color(self) -> None:
        client = ArmorClient(FakeSerial())
        client.connect()
        with self.assertRaisesRegex(ValueError, "0..255"):
            client.set_led_color(256, 0, 0)

    def test_rejects_wrong_handshake_nonce(self) -> None:
        client = ArmorClient(FakeSerial(wrong_nonce=True))
        with self.assertRaisesRegex(ConnectionError, "nonce"):
            client.connect()


if __name__ == "__main__":
    unittest.main()

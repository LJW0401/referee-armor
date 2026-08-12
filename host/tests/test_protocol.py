"""Unit tests for the desktop wire codec and frame validation boundary."""

from __future__ import annotations

import unittest

from armor_host.protocol import ProtocolError, cobs_decode, cobs_encode, decode_frame, encode_frame


class ProtocolTests(unittest.TestCase):
    """Verify COBS, CRC, and bounded frame behavior without a serial device."""

    def test_cobs_round_trip_with_zero_bytes(self) -> None:
        payload = b"\x00armor\x00protocol\x00"
        self.assertEqual(cobs_decode(cobs_encode(payload)), payload)

    def test_frame_round_trip(self) -> None:
        encoded = encode_frame(0x01, 42, bytes.fromhex("78563412"))
        frame = decode_frame(encoded[:-1])
        self.assertEqual(frame.version, 1)
        self.assertEqual(frame.frame_type, 0x01)
        self.assertEqual(frame.sequence, 42)
        self.assertEqual(frame.payload, bytes.fromhex("78563412"))

    def test_rejects_crc_corruption(self) -> None:
        encoded = bytearray(encode_frame(0x01, 1, b"test")[:-1])
        encoded[-1] ^= 0x01
        with self.assertRaisesRegex(ProtocolError, "CRC|COBS"):
            decode_frame(bytes(encoded))

    def test_rejects_malformed_cobs_frame(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "COBS"):
            cobs_decode(b"\x02")


if __name__ == "__main__":
    unittest.main()

"""Wire encoding and validation for the armor USB CDC protocol."""

from __future__ import annotations

from dataclasses import dataclass

PROTOCOL_VERSION = 1
MAX_PAYLOAD_LENGTH = 192
MAX_RAW_FRAME_LENGTH = 200
MAX_ENCODED_FRAME_LENGTH = 202
HEADER_LENGTH = 6
CRC_LENGTH = 2


class ProtocolError(ValueError):
    """Raised when a serial frame is malformed or violates the protocol."""


@dataclass(frozen=True, slots=True)
class Frame:
    """One validated, decoded protocol frame."""

    version: int
    frame_type: int
    sequence: int
    payload: bytes


def crc16_ccitt_false(data: bytes) -> int:
    """Return the CRC-16/CCITT-FALSE checksum for *data*."""

    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    """Encode a non-delimited COBS payload."""

    encoded = bytearray(b"\x00")
    code_index = 0
    code = 1
    for value in data:
        if value == 0:
            encoded[code_index] = code
            code_index = len(encoded)
            encoded.append(0)
            code = 1
            continue
        encoded.append(value)
        code += 1
        if code == 0xFF:
            encoded[code_index] = code
            code_index = len(encoded)
            encoded.append(0)
            code = 1
    encoded[code_index] = code
    return bytes(encoded)


def cobs_decode(data: bytes) -> bytes:
    """Decode one COBS payload that excludes its trailing zero delimiter."""

    if not data or len(data) > MAX_ENCODED_FRAME_LENGTH:
        raise ProtocolError("COBS frame length is invalid")
    decoded = bytearray()
    index = 0
    while index < len(data):
        code = data[index]
        index += 1
        if code == 0 or index + code - 1 > len(data):
            raise ProtocolError("COBS frame is malformed")
        decoded.extend(data[index : index + code - 1])
        index += code - 1
        if code != 0xFF and index < len(data):
            decoded.append(0)
    if len(decoded) > MAX_RAW_FRAME_LENGTH:
        raise ProtocolError("decoded frame exceeds the protocol limit")
    return bytes(decoded)


def encode_frame(frame_type: int, sequence: int, payload: bytes = b"") -> bytes:
    """Return one complete COBS-delimited protocol frame."""

    if not 0 <= frame_type <= 0xFF:
        raise ValueError("frame type must fit in one byte")
    if not 1 <= sequence <= 0xFFFF:
        raise ValueError("sequence must be in the range 1..65535")
    if len(payload) > MAX_PAYLOAD_LENGTH:
        raise ValueError("payload exceeds the protocol limit")
    raw = bytearray((PROTOCOL_VERSION, frame_type))
    raw.extend(sequence.to_bytes(2, "little"))
    raw.extend(len(payload).to_bytes(2, "little"))
    raw.extend(payload)
    raw.extend(crc16_ccitt_false(raw).to_bytes(2, "little"))
    return cobs_encode(bytes(raw)) + b"\x00"


def decode_frame(encoded: bytes) -> Frame:
    """Validate and decode one COBS payload without its zero delimiter."""

    raw = cobs_decode(encoded)
    if len(raw) < HEADER_LENGTH + CRC_LENGTH:
        raise ProtocolError("frame is shorter than its header and CRC")
    payload_length = int.from_bytes(raw[4:6], "little")
    if payload_length > MAX_PAYLOAD_LENGTH:
        raise ProtocolError("frame payload exceeds the protocol limit")
    if len(raw) != HEADER_LENGTH + payload_length + CRC_LENGTH:
        raise ProtocolError("frame length does not match its payload length")
    expected_crc = int.from_bytes(raw[-CRC_LENGTH:], "little")
    if crc16_ccitt_false(raw[:-CRC_LENGTH]) != expected_crc:
        raise ProtocolError("frame CRC does not match")
    return Frame(raw[0], raw[1], int.from_bytes(raw[2:4], "little"), raw[6:-2])

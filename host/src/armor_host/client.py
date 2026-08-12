"""Serial transport, handshake correlation, and status decoding for armor."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import secrets
import time
from typing import Protocol

import serial
from serial.tools import list_ports

from .protocol import (
    MAX_ENCODED_FRAME_LENGTH,
    PROTOCOL_VERSION,
    Frame,
    ProtocolError,
    decode_frame,
    encode_frame,
)

BAUDRATE = 115200
GET_DEVICE_INFO = 0x01
GET_STATUS = 0x02
SET_LED_COLOR = 0x10
ERROR_RESPONSE = 0xFF
RESPONSE_MASK = 0x80
HANDSHAKE_ATTEMPTS = 3
REQUEST_TIMEOUT_SECONDS = 0.5
OPEN_SETTLE_SECONDS = 0.5

ERROR_NAMES = {
    0x01: "UNSUPPORTED_VERSION",
    0x02: "UNSUPPORTED_COMMAND",
    0x03: "INVALID_PAYLOAD",
    0x04: "NOT_CONNECTED",
    0x05: "BUSY",
    0x06: "INTERNAL_ERROR",
}


class SerialConnection(Protocol):
    """Minimal pyserial boundary used by the protocol client."""

    def read(self, size: int = 1) -> bytes: ...

    def write(self, data: bytes) -> int: ...

    def reset_input_buffer(self) -> None: ...

    def close(self) -> None: ...


class ConnectionError(RuntimeError):
    """Raised when a selected serial port is not the expected ESP32 endpoint."""


@dataclass(frozen=True, slots=True)
class SerialPort:
    """A user-selectable serial port reported by the operating system."""

    device: str
    description: str
    hardware_id: str


@dataclass(frozen=True, slots=True)
class DeviceInfo:
    """Identity returned by a verified GET_DEVICE_INFO response."""

    device_id: str
    protocol_version: int
    firmware_version: str
    capabilities: int

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


@dataclass(frozen=True, slots=True)
class DeviceStatus:
    """Current device state returned by GET_STATUS."""

    status_schema: int
    health_flags: int
    uptime_ms: int
    weight_mg: int | None
    sample_age_ms: int | None
    led_count: int
    active_led_effect: int

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def list_serial_ports() -> list[SerialPort]:
    """Return stable, display-ready descriptions of available serial ports."""

    return [
        SerialPort(port.device, port.description, port.hwid)
        for port in sorted(list_ports.comports(), key=lambda item: item.device.casefold())
    ]


class ArmorClient:
    """Own one sequential USB CDC session with one armor device."""

    def __init__(self, connection: SerialConnection) -> None:
        self._connection = connection
        self._next_sequence = 1
        self._connected = False

    @classmethod
    def open(cls, port: str) -> "ArmorClient":
        """Open *port* with the fixed USB CDC settings required by the protocol."""

        try:
            connection = serial.Serial(
                port=port,
                baudrate=BAUDRATE,
                timeout=0.05,
                write_timeout=REQUEST_TIMEOUT_SECONDS,
            )
        except serial.SerialException as error:
            raise ConnectionError(f"cannot open serial port {port}: {error}") from error
        return cls(connection)

    def close(self) -> None:
        """Close the selected serial port."""

        self._connection.close()
        self._connected = False

    def __enter__(self) -> "ArmorClient":
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        del exc_type, exc, traceback
        self.close()

    def connect(self) -> DeviceInfo:
        """Complete the bounded nonce-correlated connection handshake."""

        time.sleep(OPEN_SETTLE_SECONDS)
        self._connection.reset_input_buffer()
        nonce = secrets.randbits(32)
        payload = nonce.to_bytes(4, "little")
        last_error: ConnectionError | None = None
        for _ in range(HANDSHAKE_ATTEMPTS):
            try:
                frame = self._request(GET_DEVICE_INFO, payload)
                device = self._parse_device_info(frame, nonce)
                self._connected = True
                return device
            except ConnectionError as error:
                last_error = error
        raise ConnectionError(
            "no valid GET_DEVICE_INFO response after three attempts: "
            f"{last_error}"
        ) from last_error

    def get_status(self) -> DeviceStatus:
        """Request and decode one current status snapshot after a handshake."""

        if not self._connected:
            raise ConnectionError("GET_STATUS requires a completed handshake")
        return self._parse_status(self._request(GET_STATUS))

    def set_led_color(self, red: int, green: int, blue: int) -> None:
        """Apply one static RGB color to both WS2812 strips after a handshake."""

        if not self._connected:
            raise ConnectionError("SET_LED_COLOR requires a completed handshake")
        components = (red, green, blue)
        if any(not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 255 for value in components):
            raise ValueError("RGB components must be integers in the range 0..255")
        response = self._request(SET_LED_COLOR, bytes(components))
        if response.payload:
            raise ConnectionError("SET_LED_COLOR response must have an empty payload")

    def _request(self, command: int, payload: bytes = b"") -> Frame:
        sequence = self._next_sequence
        self._next_sequence = 1 if sequence == 0xFFFF else sequence + 1
        try:
            written = self._connection.write(encode_frame(command, sequence, payload))
        except (serial.SerialException, OSError) as error:
            raise ConnectionError(f"cannot write serial request: {error}") from error
        if written <= 0:
            raise ConnectionError("serial write accepted no request bytes")
        response = self._read_frame()
        if response.sequence != sequence:
            raise ConnectionError("response sequence does not match the request")
        if response.version != PROTOCOL_VERSION:
            raise ConnectionError("response protocol version is unsupported")
        if response.frame_type == ERROR_RESPONSE:
            self._raise_device_error(command, response.payload)
        if response.frame_type != command | RESPONSE_MASK:
            raise ConnectionError("response type does not match the request")
        return response

    def _read_frame(self) -> Frame:
        deadline = time.monotonic() + REQUEST_TIMEOUT_SECONDS
        encoded = bytearray()
        while time.monotonic() < deadline:
            try:
                chunk = self._connection.read(1)
            except (serial.SerialException, OSError) as error:
                raise ConnectionError(f"cannot read serial response: {error}") from error
            if not chunk:
                continue
            if chunk == b"\x00":
                if not encoded:
                    continue
                try:
                    return decode_frame(bytes(encoded))
                except ProtocolError as error:
                    raise ConnectionError(f"invalid response frame: {error}") from error
            encoded.extend(chunk)
            if len(encoded) > MAX_ENCODED_FRAME_LENGTH:
                raise ConnectionError("response frame exceeds the protocol limit")
        raise ConnectionError("timed out waiting for a serial response")

    @staticmethod
    def _parse_device_info(frame: Frame, nonce: int) -> DeviceInfo:
        if len(frame.payload) != 20:
            raise ConnectionError("GET_DEVICE_INFO response has an invalid length")
        if int.from_bytes(frame.payload[0:4], "little") != nonce:
            raise ConnectionError("GET_DEVICE_INFO response nonce does not match")
        protocol_version = frame.payload[12]
        if protocol_version != PROTOCOL_VERSION:
            raise ConnectionError("device reports an unsupported protocol version")
        return DeviceInfo(
            device_id=f"{int.from_bytes(frame.payload[4:12], 'little'):016X}",
            protocol_version=protocol_version,
            firmware_version=".".join(str(value) for value in frame.payload[13:16]),
            capabilities=int.from_bytes(frame.payload[16:20], "little"),
        )

    @staticmethod
    def _parse_status(frame: Frame) -> DeviceStatus:
        if len(frame.payload) != 20:
            raise ConnectionError("GET_STATUS response has an invalid length")
        if frame.payload[0] != 1:
            raise ConnectionError("GET_STATUS schema is unsupported")
        if frame.payload[17:20] != b"\x00\x00\x00":
            raise ConnectionError("GET_STATUS reserved bytes are non-zero")
        weight_mg = int.from_bytes(frame.payload[7:11], "little", signed=True)
        sample_age_ms = int.from_bytes(frame.payload[11:15], "little")
        return DeviceStatus(
            status_schema=frame.payload[0],
            health_flags=int.from_bytes(frame.payload[1:3], "little"),
            uptime_ms=int.from_bytes(frame.payload[3:7], "little"),
            weight_mg=None if weight_mg == -(2**31) else weight_mg,
            sample_age_ms=None if sample_age_ms == 0xFFFFFFFF else sample_age_ms,
            led_count=frame.payload[15],
            active_led_effect=frame.payload[16],
        )

    @staticmethod
    def _raise_device_error(command: int, payload: bytes) -> None:
        if len(payload) != 2 or payload[0] != command:
            raise ConnectionError("device returned an invalid error response")
        name = ERROR_NAMES.get(payload[1], f"UNKNOWN_ERROR_{payload[1]:02X}")
        raise ConnectionError(f"device rejected command {command:#04x}: {name}")

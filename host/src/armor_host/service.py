"""Thread-safe application service that owns one browser-visible serial session."""

from __future__ import annotations

from dataclasses import asdict
from threading import Lock
from typing import Callable

from .client import ArmorClient, ConnectionError, list_serial_ports


class ServiceError(RuntimeError):
    """Raised when a browser request cannot be served from the serial session."""


class ArmorService:
    """Serialize browser operations so one USB CDC request is in flight at a time."""

    def __init__(self, client_factory: Callable[[str], ArmorClient] = ArmorClient.open) -> None:
        self._client_factory = client_factory
        self._lock = Lock()
        self._client: ArmorClient | None = None
        self._port: str | None = None
        self._device: dict[str, object] | None = None
        self._status_failures = 0

    def ports(self) -> list[dict[str, str]]:
        """List operating-system serial ports for the browser selector."""

        return [asdict(port) for port in list_serial_ports()]

    def connect(self, port: str) -> dict[str, object]:
        """Replace any prior session with a fully verified connection to *port*."""

        if not isinstance(port, str) or not port.strip():
            raise ServiceError("a serial port must be selected")
        with self._lock:
            self._disconnect_locked()
            try:
                client = self._client_factory(port)
                device = client.connect()
                status = client.get_status()
            except ConnectionError as error:
                if "client" in locals():
                    client.close()
                raise ServiceError(str(error)) from error
            self._client = client
            self._port = port
            self._device = device.to_dict()
            self._status_failures = 0
            return self._snapshot_locked(status.to_dict())

    def status(self) -> dict[str, object]:
        """Return a fresh status snapshot or disconnect after two read failures."""

        with self._lock:
            if self._client is None:
                raise ServiceError("no ESP32 serial session is connected")
            try:
                status = self._client.get_status().to_dict()
            except ConnectionError as error:
                self._status_failures += 1
                if self._status_failures >= 2:
                    self._disconnect_locked()
                raise ServiceError(str(error)) from error
            self._status_failures = 0
            return self._snapshot_locked(status)

    def disconnect(self) -> None:
        """Close the active serial session, if any."""

        with self._lock:
            self._disconnect_locked()

    def _snapshot_locked(self, status: dict[str, object]) -> dict[str, object]:
        return {"port": self._port, "device": self._device, "status": status}

    def _disconnect_locked(self) -> None:
        if self._client is not None:
            self._client.close()
        self._client = None
        self._port = None
        self._device = None
        self._status_failures = 0

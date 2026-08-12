"""Unit tests for serial-session ownership used by the browser API."""

from __future__ import annotations

import unittest

from armor_host.client import ConnectionError, DeviceInfo, DeviceStatus
from armor_host.service import ArmorService, ServiceError


class FakeClient:
    """Controlled endpoint that models a connected serial client."""

    def __init__(self, fail_status: bool = False) -> None:
        self.fail_status = fail_status
        self.closed = False

    def connect(self) -> DeviceInfo:
        return DeviceInfo("0000000000000001", 1, "1.0.0", 0)

    def get_status(self) -> DeviceStatus:
        if self.fail_status:
            raise ConnectionError("serial read failed")
        return DeviceStatus(1, 0, 10, None, None, 0, 0, 128, 0, 255)

    def close(self) -> None:
        self.closed = True

    def set_led_color(self, red: int, green: int, blue: int) -> None:
        self.color = (red, green, blue)


class ArmorServiceTests(unittest.TestCase):
    """Verify only verified sessions become browser-visible."""

    def test_connect_returns_identity_and_initial_status(self) -> None:
        client = FakeClient()
        service = ArmorService(lambda port: client)
        snapshot = service.connect("COM3")
        self.assertEqual(snapshot["port"], "COM3")
        self.assertEqual(snapshot["device"]["device_id"], "0000000000000001")
        self.assertIsNone(snapshot["status"]["weight_mg"])

    def test_second_status_failure_disconnects_session(self) -> None:
        client = FakeClient(fail_status=True)
        service = ArmorService(lambda port: client)
        service._client = client
        service._port = "COM3"
        service._device = {"device_id": "x"}
        with self.assertRaises(ServiceError):
            service.status()
        with self.assertRaises(ServiceError):
            service.status()
        self.assertTrue(client.closed)

    def test_sets_color_then_returns_fresh_status(self) -> None:
        client = FakeClient()
        service = ArmorService(lambda port: client)
        service.connect("COM3")
        service.set_led_color(128, 0, 255)
        self.assertEqual(client.color, (128, 0, 255))


if __name__ == "__main__":
    unittest.main()

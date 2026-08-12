"""HTTP smoke tests for the localhost browser-interface boundary."""

from __future__ import annotations

from http import HTTPStatus
from threading import Thread
import unittest
from urllib.error import HTTPError
from urllib.request import urlopen

from armor_host.server import ArmorHttpServer


class StaticService:
    """Minimal service replacement that avoids opening a physical serial port."""

    def ports(self) -> list[dict[str, str]]:
        return [{"device": "COM3", "description": "ESP32", "hardware_id": "test"}]

    def status(self) -> dict[str, object]:
        raise RuntimeError("no connection")

    def disconnect(self) -> None:
        pass


class BrowserServerTests(unittest.TestCase):
    """Verify the page and API route from a real loopback HTTP client."""

    def setUp(self) -> None:
        self.server = ArmorHttpServer(("127.0.0.1", 0), StaticService())
        self.thread = Thread(target=self.server.serve_forever)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def test_serves_page_and_serial_port_api(self) -> None:
        with urlopen(f"{self.base_url}/") as response:
            self.assertEqual(response.status, HTTPStatus.OK)
            self.assertIn("Armor 控制台", response.read().decode("utf-8"))
        with urlopen(f"{self.base_url}/api/ports") as response:
            self.assertEqual(response.status, HTTPStatus.OK)
            self.assertIn(b'"COM3"', response.read())

    def test_rejects_path_outside_static_directory(self) -> None:
        with self.assertRaises(HTTPError) as captured:
            urlopen(f"{self.base_url}/../pyproject.toml")
        self.assertEqual(captured.exception.code, HTTPStatus.NOT_FOUND)


if __name__ == "__main__":
    unittest.main()

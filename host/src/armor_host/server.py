"""Localhost HTTP server and JSON API for the armor browser interface."""

from __future__ import annotations

from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from typing import NoReturn
from urllib.parse import urlparse

from .service import ArmorService, ServiceError

STATIC_DIRECTORY = Path(__file__).with_name("static")
MAX_JSON_BODY_BYTES = 1024


class ArmorHttpServer(ThreadingHTTPServer):
    """HTTP server carrying the one shared serial-session service."""

    def __init__(self, address: tuple[str, int], service: ArmorService) -> None:
        super().__init__(address, ArmorRequestHandler)
        self.service = service


class ArmorRequestHandler(BaseHTTPRequestHandler):
    """Serve static UI files and the minimal local serial-control API."""

    server: ArmorHttpServer

    def do_GET(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path == "/api/ports":
            self._send_json(HTTPStatus.OK, {"ports": self.server.service.ports()})
            return
        if path == "/api/status":
            self._serve_status()
            return
        self._serve_static(path)

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path
        if path == "/api/connect":
            body = self._read_json_body()
            if body is None:
                return
            try:
                snapshot = self.server.service.connect(body.get("port"))
            except ServiceError as error:
                self._send_json(HTTPStatus.BAD_GATEWAY, {"error": str(error)})
                return
            self._send_json(HTTPStatus.OK, snapshot)
            return
        if path == "/api/disconnect":
            self.server.service.disconnect()
            self._send_json(HTTPStatus.NO_CONTENT, None)
            return
        self._send_json(HTTPStatus.NOT_FOUND, {"error": "endpoint not found"})

    def log_message(self, format: str, *args: object) -> None:
        """Suppress per-request console logs; command output remains actionable."""

        del format, args

    def _serve_status(self) -> None:
        try:
            snapshot = self.server.service.status()
        except ServiceError as error:
            self._send_json(HTTPStatus.CONFLICT, {"error": str(error)})
            return
        self._send_json(HTTPStatus.OK, snapshot)

    def _read_json_body(self) -> dict[str, object] | None:
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            self._send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid Content-Length"})
            return None
        if length < 2 or length > MAX_JSON_BODY_BYTES:
            self._send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid JSON body size"})
            return None
        try:
            body = json.loads(self.rfile.read(length))
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._send_json(HTTPStatus.BAD_REQUEST, {"error": "invalid JSON body"})
            return None
        if not isinstance(body, dict):
            self._send_json(HTTPStatus.BAD_REQUEST, {"error": "JSON body must be an object"})
            return None
        return body

    def _serve_static(self, path: str) -> None:
        requested = "index.html" if path == "/" else path.removeprefix("/")
        candidate = (STATIC_DIRECTORY / requested).resolve()
        if STATIC_DIRECTORY.resolve() not in candidate.parents or not candidate.is_file():
            self._send_json(HTTPStatus.NOT_FOUND, {"error": "file not found"})
            return
        content_type = {
            ".html": "text/html; charset=utf-8",
            ".css": "text/css; charset=utf-8",
            ".js": "text/javascript; charset=utf-8",
        }.get(candidate.suffix)
        if content_type is None:
            self._send_json(HTTPStatus.NOT_FOUND, {"error": "unsupported static file"})
            return
        content = candidate.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)

    def _send_json(self, status: HTTPStatus, payload: object) -> None:
        if status == HTTPStatus.NO_CONTENT:
            self.send_response(status)
            self.end_headers()
            return
        content = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(content)))
        self.end_headers()
        self.wfile.write(content)


def serve(port: int) -> NoReturn:
    """Serve the browser UI only on localhost until the process is stopped."""

    server = ArmorHttpServer(("127.0.0.1", port), ArmorService())
    print(f"Armor host UI is available at http://127.0.0.1:{port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.service.disconnect()
        server.server_close()
    raise SystemExit(0)

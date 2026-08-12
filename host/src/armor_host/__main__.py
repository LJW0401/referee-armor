"""Command-line entry point for listing, connecting to, and monitoring armor."""

from __future__ import annotations

import argparse
from dataclasses import asdict
import json
import sys
import time

from .client import ArmorClient, ConnectionError, list_serial_ports
from .server import serve


def build_parser() -> argparse.ArgumentParser:
    """Build the bounded command-line interface."""

    parser = argparse.ArgumentParser(prog="armor-host")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("ports", help="list selectable serial ports")
    connect = commands.add_parser("connect", help="handshake with one ESP32")
    connect.add_argument("--port", required=True, help="serial port, for example COM5")
    connect.add_argument(
        "--watch", action="store_true", help="refresh status at 10 Hz until Ctrl+C"
    )
    serve_command = commands.add_parser("serve", help="start the local browser interface")
    serve_command.add_argument("--port", type=int, default=8080, help="localhost HTTP port")
    return parser


def main() -> int:
    """Run the selected host command and return a process exit status."""

    arguments = build_parser().parse_args()
    if arguments.command == "ports":
        print(json.dumps([asdict(port) for port in list_serial_ports()], ensure_ascii=False, indent=2))
        return 0
    if arguments.command == "serve":
        if not 1 <= arguments.port <= 65535:
            parser = build_parser()
            parser.error("--port must be in the range 1..65535")
        serve(arguments.port)

    try:
        with ArmorClient.open(arguments.port) as client:
            device = client.connect()
            print(json.dumps({"connected": device.to_dict()}, ensure_ascii=False))
            while True:
                print(json.dumps({"status": client.get_status().to_dict()}, ensure_ascii=False))
                if not arguments.watch:
                    return 0
                time.sleep(0.1)
    except ConnectionError as error:
        print(f"armor-host: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())

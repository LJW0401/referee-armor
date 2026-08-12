"""Command-line entry point for listing, connecting to, and monitoring armor."""

from __future__ import annotations

import argparse
from dataclasses import asdict
import json
import sys
import time

from .client import ArmorClient, ConnectionError, list_serial_ports


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
    return parser


def main() -> int:
    """Run the selected host command and return a process exit status."""

    arguments = build_parser().parse_args()
    if arguments.command == "ports":
        print(json.dumps([asdict(port) for port in list_serial_ports()], ensure_ascii=False, indent=2))
        return 0

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

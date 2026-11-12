# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Command-line entry point for PicoUart HID diagnostics and board controls."""

import argparse
import json
import math
import sys
import time
from collections.abc import Callable

from .hid.client import PicoUartHid
from .hid.types import StatusSnapshot


def print_status(status: StatusSnapshot) -> None:
    """Print one concise per-channel update for monitor and status commands."""
    channels = " ".join(
        f"cdc{channel['id']} "
        + (
            f"state={channel['state']} measurement={channel['uart_measurement_state']} "
            f"seq={channel['sequence']} "
            f"uart_tx/rx={channel['traffic']['uart_tx']}/{channel['traffic']['uart_rx']} "
            f"cdc_tx/rx={channel['traffic']['usb_tx']}/{channel['traffic']['usb_rx']}"
            if channel["sequence"] is not None else "waiting-for-input"
        )
        for channel in status["channels"]
    )
    print(channels)


def monitor(client: PicoUartHid, duration: float) -> None:
    """Print valid status reports until the requested duration expires."""
    deadline = time.monotonic() + duration
    valid_reports = 0
    while True:
        remaining_s = deadline - time.monotonic()
        if remaining_s <= 0:
            break
        timeout_ms = max(1, min(250, math.ceil(remaining_s * 1000)))
        status = client.read_status(timeout_ms=timeout_ms, deadline=deadline)
        if status is None or time.monotonic() >= deadline:
            continue
        print_status(status)
        valid_reports += 1
    if valid_reports == 0:
        raise RuntimeError("monitor timed out without receiving a valid status report")


def _status(client: PicoUartHid, arguments: argparse.Namespace) -> None:
    deadline = time.monotonic() + arguments.timeout
    status = None
    while status is None and time.monotonic() < deadline:
        remaining_ms = max(1, min(250, int((deadline - time.monotonic()) * 1000)))
        status = client.read_status(remaining_ms, deadline=deadline)
    if status is None:
        raise RuntimeError("timed out without receiving a valid status report")
    if arguments.json:
        print(json.dumps(status, sort_keys=True))
    else:
        print_status(status)


COMMANDS: dict[str, Callable[[PicoUartHid, argparse.Namespace], None]] = {
    "monitor": lambda client, arguments: monitor(client, arguments.duration),
    "status": _status,
    "temperature": lambda client, _: print(f"temperature={client.read_board_temperature():.2f} C"),
    "version": lambda client, _: print(client.read_firmware_version()),
    "overruns": lambda client, _: print(
        " ".join(f"cdc{index}={count}" for index, count in enumerate(client.read_overflow_counts()))
    ),
    "hardware": lambda client, _: print(json.dumps(client.read_hardware_info(), sort_keys=True)),
    "config": lambda client, _: print(json.dumps(client.read_configuration(), sort_keys=True)),
    "toggle-led": lambda client, _: client.toggle_led(),
    "reset": lambda client, _: client.reset_board(),
}


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    """Parse the HID command-line interface."""
    parser = argparse.ArgumentParser(prog="pico-uart", description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--serial", help="select a PicoUart HID interface by USB serial number")
    selection.add_argument("--device-path", help="select a PicoUart HID interface by hidapi path")
    commands = parser.add_subparsers(dest="command", required=True)

    monitor_parser = commands.add_parser("monitor", help="print periodic status reports")
    monitor_parser.add_argument("--duration", type=float, default=5.0, help="monitor duration in seconds")
    status_parser = commands.add_parser("status", help="read one channel status report")
    status_parser.add_argument(
        "--timeout", type=float, default=2.0,
        help="status acquisition timeout in seconds after opening the HID device",
    )
    status_parser.add_argument("--json", action="store_true", help="print status as JSON")
    commands.add_parser("temperature", help="read the internal board temperature")
    commands.add_parser("version", help="read the firmware semantic version (MAJOR.MINOR.PATCH)")
    commands.add_parser("overruns", help="read cumulative UART RX dropped-byte counts")
    commands.add_parser("hardware", help="read MCU identity and current system clock as JSON")
    commands.add_parser("config", help="read persistent configuration diagnostics as JSON")
    commands.add_parser("toggle-led", help="toggle the board's default LED")
    commands.add_parser(
        "reset",
        help="request a confirmed board reboot (requires firmware HID reset support)",
    )
    dashboard_parser = commands.add_parser(
        "dashboard", aliases=["web"], help="start the local diagnostics dashboard"
    )
    dashboard_parser.add_argument("--port", type=int, default=5000, help="local dashboard port")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """Run the selected PicoUart HID command."""
    arguments = parse_arguments(argv)
    if arguments.command == "monitor" and (not math.isfinite(arguments.duration) or arguments.duration <= 0):
        raise SystemExit("--duration must be a finite value greater than zero")
    if arguments.command == "status" and (not math.isfinite(arguments.timeout) or arguments.timeout <= 0):
        raise SystemExit("--timeout must be a finite value greater than zero")
    if arguments.command in ("dashboard", "web"):
        if not 1 <= arguments.port <= 65535:
            raise SystemExit("--port must be between 1 and 65535")
        try:
            from .dashboard.app import run_server
            run_server(arguments.port, arguments.serial, arguments.device_path)
        except ModuleNotFoundError as error:
            if error.name != "flask":
                raise
            print("error: Flask is missing; reinstall with `python -m pip install pico-uart`", file=sys.stderr)
            return 1
        except (OSError, RuntimeError) as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        return 0

    try:
        with PicoUartHid(arguments.serial, arguments.device_path) as client:
            COMMANDS[arguments.command](client, arguments)
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

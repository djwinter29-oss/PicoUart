"""Command-line interface for PicoUart HID diagnostics and board controls."""

import argparse
import json
import math
import sys
import time

from .client import PicoUartHid
from .protocol import decode_health


def print_status(status: dict[str, object]) -> None:
    """Print one concise status report for monitor and status commands."""
    channels = " ".join(
        f"cdc{channel['id']} health=0x{channel['health']:02x}"
        f"[{','.join(decode_health(channel['health'])) or '-'}] "
        f"uart_tx/rx={channel['controller_tx_bytes']}/{channel['controller_rx_bytes']} "
        f"cdc_tx/rx={channel['cdc_tx_bytes']}/{channel['cdc_rx_bytes']} "
        f"ring_peak={channel['ring_high_watermark']}"
        for channel in status["channels"]
    )
    print(f"seq={status['sequence']} {channels}")


def monitor(client: PicoUartHid, duration: float) -> None:
    """Print valid status reports until the requested duration expires."""
    deadline = time.monotonic() + duration
    valid_reports = 0
    while time.monotonic() < deadline:
        status = client.read_status(timeout_ms=250)
        if status is None:
            continue
        print_status(status)
        valid_reports += 1
    if valid_reports == 0:
        raise RuntimeError("monitor timed out without receiving a valid status report")


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    """Parse the HID command-line interface."""
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--serial", help="select a PicoUart HID interface by USB serial number")
    selection.add_argument("--device-path", help="select a PicoUart HID interface by hidapi path")
    commands = parser.add_subparsers(dest="command", required=True)

    monitor_parser = commands.add_parser("monitor", help="print periodic status reports")
    monitor_parser.add_argument("--duration", type=float, default=5.0, help="monitor duration in seconds")
    status_parser = commands.add_parser("status", help="read one channel status report")
    status_parser.add_argument("--timeout", type=float, default=2.0, help="status read timeout in seconds")
    status_parser.add_argument("--json", action="store_true", help="print status as JSON")
    commands.add_parser("temperature", help="read the internal board temperature")
    commands.add_parser("version", help="read the firmware semantic version (MAJOR.MINOR.PATCH)")
    commands.add_parser("overruns", help="read cumulative UART RX dropped-byte counts")
    commands.add_parser("toggle-led", help="toggle the board's default LED")
    commands.add_parser(
        "reset",
        help="arm then reset the board (requires firmware HID reset support)",
    )
    web_parser = commands.add_parser("web", help="start the local Flask diagnostics dashboard")
    web_parser.add_argument("--port", type=int, default=5000, help="local web server port")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    """Run the selected PicoUart HID command."""
    arguments = parse_arguments(argv)
    if arguments.command == "monitor" and (
        not math.isfinite(arguments.duration) or arguments.duration <= 0
    ):
        raise SystemExit("--duration must be a finite value greater than zero")
    if arguments.command == "status" and (
        not math.isfinite(arguments.timeout) or arguments.timeout <= 0
    ):
        raise SystemExit("--timeout must be a finite value greater than zero")
    if arguments.command == "web":
        if not 1 <= arguments.port <= 65535:
            raise SystemExit("--port must be between 1 and 65535")
        try:
            from .web.app import run_server

            run_server(arguments.port, arguments.serial, arguments.device_path)
        except (OSError, RuntimeError) as error:
            print(f"error: {error}", file=sys.stderr)
            return 1
        return 0

    try:
        with PicoUartHid(arguments.serial, arguments.device_path) as client:
            if arguments.command == "monitor":
                monitor(client, arguments.duration)
            elif arguments.command == "status":
                deadline = time.monotonic() + arguments.timeout
                status = None
                while status is None and time.monotonic() < deadline:
                    remaining_ms = max(1, min(250, int((deadline - time.monotonic()) * 1000)))
                    status = client.read_status(remaining_ms)
                if status is None:
                    raise RuntimeError("timed out without receiving a valid status report")
                if arguments.json:
                    print(json.dumps(status, sort_keys=True))
                else:
                    print_status(status)
            elif arguments.command == "temperature":
                print(f"temperature={client.read_board_temperature():.2f} C")
            elif arguments.command == "version":
                print(client.read_firmware_version())
            elif arguments.command == "overruns":
                print(" ".join(f"cdc{index}={count}" for index, count in enumerate(client.read_overflow_counts())))
            elif arguments.command == "toggle-led":
                client.toggle_led()
            elif arguments.command == "reset":
                client.reset_board()
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0
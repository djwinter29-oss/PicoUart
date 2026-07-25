#!/usr/bin/env python3
"""Control and monitor PicoUart's vendor HID interface."""

import argparse
import struct
import sys
import time
from typing import Any

try:
    import hid
except ImportError as error:
    raise SystemExit("Missing dependency: install host/python/requirements.txt") from error

VENDOR_ID = 0xCAFE
PRODUCT_ID = 0x4010
USAGE_PAGE = 0xFF00
USAGE = 0x0001

REPORT_ID_STATUS = 1
REPORT_ID_BOARD_STATUS = 3
REPORT_ID_COMMAND = 4

COMMAND_TOGGLE_LED = 1
COMMAND_RESET_BOARD = 2

STATUS_SIZE = 64
BOARD_STATUS_SIZE = 4


def open_device() -> Any:
    """Open PicoUart's vendor-defined HID collection."""
    devices = hid.enumerate(VENDOR_ID, PRODUCT_ID)
    for device_info in devices:
        if device_info.get("usage_page") == USAGE_PAGE and device_info.get("usage") == USAGE:
            device = hid.device()
            device.open_path(device_info["path"])
            return device

    raise RuntimeError(
        "PicoUart HID interface not found. Check that the firmware is connected and enumerated."
    )


def require_payload(report: list[int], report_id: int, payload_size: int) -> bytes:
    """Validate hidapi's report-ID-prefixed feature report response."""
    if len(report) != payload_size + 1 or report[0] != report_id:
        raise RuntimeError(
            f"unexpected report {report_id}: expected {payload_size + 1} bytes with report ID prefix, "
            f"received {len(report)} bytes"
        )
    return bytes(report[1:])


def read_feature(device: Any, report_id: int, payload_size: int) -> bytes:
    """Read a fixed-size feature report, excluding its HID report ID."""
    return require_payload(device.get_feature_report(report_id, payload_size + 1), report_id, payload_size)


def read_board_temperature(device: Any) -> float:
    """Read the internal RP2 temperature estimate in degrees Celsius."""
    version, _reserved, centidegrees = struct.unpack("<BBh", read_feature(
        device, REPORT_ID_BOARD_STATUS, BOARD_STATUS_SIZE
    ))
    if version != 13:
        raise RuntimeError(f"unsupported board-status report version {version}")
    return centidegrees / 100.0


def parse_status(payload: bytes) -> dict[str, Any]:
    """Decode the 64-byte periodic monitor report."""
    if len(payload) != STATUS_SIZE:
        raise RuntimeError(f"unexpected status report size {len(payload)}")

    signature0, signature1, version, sequence = payload[:4]
    if bytes((signature0, signature1)) != b"PU":
        raise RuntimeError("received a status report with an invalid signature")
    if version != 13:
        raise RuntimeError(f"unsupported status report version {version}")

    channels = [struct.unpack_from("<BB4H", payload, 4 + index * 10) for index in range(6)]
    return {
        "sequence": sequence,
        "channels": [
            {"id": index, "health": channel[0], "ring_high_watermark": channel[1] * 16,
             "controller_tx_bytes": channel[2], "controller_rx_bytes": channel[3],
             "cdc_tx_bytes": channel[4], "cdc_rx_bytes": channel[5]}
            for index, channel in enumerate(channels)
        ],
    }


def print_status(status: dict[str, Any]) -> None:
    """Print a concise one-line status report for repeated polling."""
    channels = " ".join(
        f"cdc{channel['id']} health=0x{channel['health']:02x} "
        f"uart_tx/rx={channel['controller_tx_bytes']}/{channel['controller_rx_bytes']} "
        f"cdc_tx/rx={channel['cdc_tx_bytes']}/{channel['cdc_rx_bytes']} "
        f"ring_peak={channel['ring_high_watermark']}"
        for channel in status["channels"]
    )
    print(f"seq={status['sequence']} {channels}")


def monitor(device: Any, duration: float) -> None:
    """Print periodic status reports for the requested duration."""
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        report = device.read(STATUS_SIZE + 1, 250)
        if not report or report[0] != REPORT_ID_STATUS:
            continue
        print_status(parse_status(bytes(report[1:])))


def send_command(device: Any, command: int) -> None:
    """Send a one-byte command through the HID feature-report control path."""
    bytes_written = device.send_feature_report([REPORT_ID_COMMAND, command])
    if bytes_written != 2:
        raise RuntimeError(f"HID command write was incomplete: wrote {bytes_written} bytes")


def parse_arguments() -> argparse.Namespace:
    """Parse the HID command-line interface."""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    monitor_parser = commands.add_parser("monitor", help="print periodic status reports")
    monitor_parser.add_argument("--duration", type=float, default=5.0, help="monitor duration in seconds")
    commands.add_parser("temperature", help="read the internal board temperature")
    commands.add_parser("toggle-led", help="toggle the board's default LED")
    commands.add_parser("reset", help="reset the board immediately")
    return parser.parse_args()


def main() -> int:
    """Run the selected PicoUart HID command."""
    arguments = parse_arguments()
    if arguments.command == "monitor" and arguments.duration <= 0:
        raise SystemExit("--duration must be greater than zero")

    try:
        device = open_device()
        try:
            if arguments.command == "monitor":
                monitor(device, arguments.duration)
            elif arguments.command == "temperature":
                print(f"temperature={read_board_temperature(device):.2f} C")
            elif arguments.command == "toggle-led":
                send_command(device, COMMAND_TOGGLE_LED)
            elif arguments.command == "reset":
                send_command(device, COMMAND_RESET_BOARD)
        finally:
            device.close()
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

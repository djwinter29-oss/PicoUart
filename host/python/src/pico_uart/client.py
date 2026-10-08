"""Resource-owning Python API for PicoUart's HID status and board controls."""

import threading
import time
from typing import Any

from .protocol import (
    BOARD_STATUS_FLAG_HID_RESET,
    BOARD_STATUS_SIZE,
    COMMAND_ARM_RESET,
    COMMAND_RESET_BOARD,
    COMMAND_TOGGLE_LED,
    OVERFLOW_COUNTS_SIZE,
    REPORT_ID_BOARD_STATUS,
    REPORT_ID_COMMAND,
    REPORT_ID_OVERFLOW_COUNTS,
    REPORT_ID_STATUS,
    RESET_ARM_WINDOW_S,
    STATUS_SIZE,
    parse_board_status,
    parse_overflow_counts,
    parse_status,
    require_payload,
)
from .transport import open_device


def read_feature(device: Any, report_id: int, payload_size: int) -> bytes:
    """Read a fixed-size feature report, excluding its HID report ID."""
    return require_payload(
        device.get_feature_report(report_id, payload_size + 1),
        report_id,
        payload_size,
    )


def read_board_status(device: Any) -> dict[str, object]:
    """Read temperature, firmware version, and board capabilities."""
    payload = read_feature(device, REPORT_ID_BOARD_STATUS, BOARD_STATUS_SIZE)
    return parse_board_status(payload)


def read_board_temperature(device: Any) -> float:
    """Read the internal RP2 temperature estimate in degrees Celsius."""
    return float(read_board_status(device)["temperature_celsius"])


def read_firmware_version(device: Any) -> str:
    """Read the firmware semantic version (MAJOR.MINOR.PATCH) from HID."""
    return str(read_board_status(device)["firmware_version"])


def read_overflow_counts(device: Any) -> list[int]:
    """Read cumulative UART-to-USB dropped-byte counts for every channel."""
    payload = read_feature(device, REPORT_ID_OVERFLOW_COUNTS, OVERFLOW_COUNTS_SIZE)
    return parse_overflow_counts(payload)


def send_command(device: Any, command: int) -> None:
    """Send a one-byte board command through the HID feature-report path."""
    bytes_written = device.send_feature_report([REPORT_ID_COMMAND, command])
    if bytes_written != 2:
        raise RuntimeError(f"HID command write was incomplete: wrote {bytes_written} bytes")


def reset_board(device: Any) -> None:
    """Arm then reset the board only when firmware enables remote reset."""
    if not read_board_status(device)["hid_reset_enabled"]:
        raise RuntimeError(
            "firmware HID reset is disabled; rebuild with -DPICO_UART_ALLOW_HID_RESET=1"
        )
    send_command(device, COMMAND_ARM_RESET)
    time.sleep(min(0.05, RESET_ARM_WINDOW_S / 10.0))
    send_command(device, COMMAND_RESET_BOARD)


class PicoUartHid:
    """Own a PicoUart HID connection and expose decoded status and controls."""

    def __init__(
        self,
        serial_number: str | None = None,
        device_path: str | None = None,
        device: Any | None = None,
    ) -> None:
        if device is not None and (serial_number is not None or device_path is not None):
            raise ValueError("an injected device cannot be combined with a device selector")
        self._device = device if device is not None else open_device(serial_number, device_path)
        self._lock = threading.RLock()
        self._closed = False

    def _require_open(self) -> Any:
        if self._closed:
            raise RuntimeError("PicoUart HID connection is closed")
        return self._device

    def read_status(self, timeout_ms: int = 250) -> dict[str, object] | None:
        """Read one periodic channel report, returning None if none is ready."""
        if timeout_ms < 0:
            raise ValueError("timeout_ms must not be negative")
        with self._lock:
            report = self._require_open().read(STATUS_SIZE + 1, timeout_ms)
        if not report:
            return None
        if report[0] != REPORT_ID_STATUS:
            raise RuntimeError(f"unexpected HID report ID {report[0]}")
        return parse_status(bytes(report[1:]))

    def read_board_status(self) -> dict[str, object]:
        with self._lock:
            return read_board_status(self._require_open())

    def read_board_temperature(self) -> float:
        return float(self.read_board_status()["temperature_celsius"])

    def read_firmware_version(self) -> str:
        return str(self.read_board_status()["firmware_version"])

    def read_overflow_counts(self) -> list[int]:
        with self._lock:
            return read_overflow_counts(self._require_open())

    def toggle_led(self) -> None:
        with self._lock:
            send_command(self._require_open(), COMMAND_TOGGLE_LED)

    def reset_board(self) -> None:
        with self._lock:
            reset_board(self._require_open())

    def close(self) -> None:
        with self._lock:
            if not self._closed:
                self._device.close()
                self._closed = True

    def __enter__(self) -> "PicoUartHid":
        self._require_open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
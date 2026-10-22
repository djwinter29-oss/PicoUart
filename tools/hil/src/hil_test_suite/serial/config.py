"""Shared raw serial-port setup for Linux hardware tests."""

from __future__ import annotations

import fcntl
import os
import termios
import time


BAUD_RATES = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
    230400: termios.B230400,
    460800: termios.B460800,
    921600: termios.B921600,
    1000000: termios.B1000000,
}
STANDARD_BAUD_BY_SETTING = {setting: rate for rate, setting in BAUD_RATES.items()}
STANDARD_BAUD_RATES = tuple(BAUD_RATES)
MAX_BAUD_DEVIATION = 0.5
PORT_OPEN_SETTLE_SECONDS = 0.25
LINE_CODING_SETTLE_SECONDS = 0.25
TCGETS2 = 0x802C542A
TCSETS2 = 0x402C542B
BOTHER = 0x1000
CBAUD = termios.CBAUD


def verify_line_speed(file_descriptor: int, baud_rate: int) -> None:
    """Verify the host TTY reports the requested input and output speed.

    This checks host-side termios state only. A successful HIL link test is
    still required to prove that firmware applied the CDC line-coding request.
    """
    if baud_rate in BAUD_RATES:
        settings = termios.tcgetattr(file_descriptor)
        actual = (STANDARD_BAUD_BY_SETTING.get(settings[4]), STANDARD_BAUD_BY_SETTING.get(settings[5]))
    else:
        raw = bytearray(44)
        fcntl.ioctl(file_descriptor, TCGETS2, raw, True)
        actual = (
            int.from_bytes(raw[36:40], "little"),
            int.from_bytes(raw[40:44], "little"),
        )
    if any(
        actual_rate is None or abs(actual_rate - baud_rate) / baud_rate >= MAX_BAUD_DEVIATION
        for actual_rate in actual
    ):
        actual_text = "/".join(str(rate) if rate is not None else "unknown" for rate in actual)
        raise OSError(
            f"serial TTY reports {actual_text} baud; requested {baud_rate} "
            f"(deviation must be less than {MAX_BAUD_DEVIATION:.0%})"
        )


def open_port(path: str, baud_rate: int, *, allow_arbitrary: bool = False) -> tuple[int, list]:
    """Open a serial port and capture settings needed to restore its TTY state."""
    if baud_rate not in BAUD_RATES and not allow_arbitrary:
        raise ValueError(f"unsupported standard baud rate: {baud_rate}")

    file_descriptor = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        original_settings = termios.tcgetattr(file_descriptor)
        original_termios2 = None
        if baud_rate not in BAUD_RATES:
            original_termios2 = bytearray(44)
            fcntl.ioctl(file_descriptor, TCGETS2, original_termios2, True)
            # BOTHER speeds cannot be restored via termios.tcsetattr; store a
            # valid termios view carrying the original settings for the shared closer.
            original_settings[4] = termios.B9600
            original_settings[5] = termios.B9600
        return file_descriptor, (original_settings, original_termios2) if original_termios2 is not None else original_settings
    except BaseException:
        os.close(file_descriptor)
        raise


def set_port_line_coding(file_descriptor: int, baud_rate: int, *, allow_arbitrary: bool = False) -> None:
    """Set a raw 8N1 line coding and verify the host TTY readback."""
    if baud_rate not in BAUD_RATES and not allow_arbitrary:
        raise ValueError(f"unsupported standard baud rate: {baud_rate}")
    settings = termios.tcgetattr(file_descriptor)
    if baud_rate in BAUD_RATES:
        settings[0] = 0
        settings[1] = 0
        settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        settings[3] = 0
        settings[4] = BAUD_RATES[baud_rate]
        settings[5] = BAUD_RATES[baud_rate]
        settings[6][termios.VMIN] = 0
        settings[6][termios.VTIME] = 0
        termios.tcsetattr(file_descriptor, termios.TCSANOW, settings)
    else:
        # CDC ACM can reject an intermediate standard-baud tcsetattr; apply
        # raw mode and BOTHER together through Linux termios2 instead.
        raw = bytearray(44)
        fcntl.ioctl(file_descriptor, TCGETS2, raw, True)
        cflag = int.from_bytes(raw[8:12], "little")
        cflag &= ~(CBAUD | termios.CSIZE | termios.PARENB | termios.CSTOPB)
        cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL | BOTHER
        raw[0:4] = (0).to_bytes(4, "little")
        raw[4:8] = (0).to_bytes(4, "little")
        raw[8:12] = cflag.to_bytes(4, "little")
        raw[12:16] = (0).to_bytes(4, "little")
        raw[17 + termios.VMIN] = 0
        raw[17 + termios.VTIME] = 0
        raw[36:40] = baud_rate.to_bytes(4, "little")
        raw[40:44] = baud_rate.to_bytes(4, "little")
        fcntl.ioctl(file_descriptor, TCSETS2, raw)
    verify_line_speed(file_descriptor, baud_rate)


def configure_port(path: str, baud_rate: int, *, allow_arbitrary: bool = False) -> tuple[int, list]:
    """Open, settle, configure a raw 8N1 serial port, and return its old settings."""
    file_descriptor, original = open_port(path, baud_rate, allow_arbitrary=allow_arbitrary)
    try:
        time.sleep(PORT_OPEN_SETTLE_SECONDS)
        set_port_line_coding(file_descriptor, baud_rate, allow_arbitrary=allow_arbitrary)
        time.sleep(LINE_CODING_SETTLE_SECONDS)
        termios.tcflush(file_descriptor, termios.TCIOFLUSH)
        return file_descriptor, original
    except BaseException:
        try:
            if isinstance(original, tuple):
                termios.tcsetattr(file_descriptor, termios.TCSANOW, original[0])
                fcntl.ioctl(file_descriptor, TCSETS2, original[1])
            else:
                termios.tcsetattr(file_descriptor, termios.TCSANOW, original)
        finally:
            os.close(file_descriptor)
        raise

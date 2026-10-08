"""Shared raw serial-port setup for Linux hardware tests."""

from __future__ import annotations

import fcntl
import os
import termios


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
STANDARD_BAUD_RATES = tuple(BAUD_RATES)
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
        expected = (BAUD_RATES[baud_rate], BAUD_RATES[baud_rate])
        actual = (settings[4], settings[5])
    else:
        raw = bytearray(44)
        fcntl.ioctl(file_descriptor, TCGETS2, raw, True)
        actual = (
            int.from_bytes(raw[36:40], "little"),
            int.from_bytes(raw[40:44], "little"),
        )
        expected = (baud_rate, baud_rate)

    if actual != expected:
        raise OSError(f"serial TTY reports {actual[0]}/{actual[1]} baud; requested {baud_rate}")


def configure_port(
    path: str,
    baud_rate: int,
    *,
    allow_arbitrary: bool = False,
) -> tuple[int, list]:
    """Open a raw 8N1 serial port, set and verify its baud, and return old settings.

    Arbitrary baud rates use Linux termios2/BOTHER and are opt-in. The caller
    must restore the returned settings and close the descriptor after testing.
    """
    if baud_rate not in BAUD_RATES and not allow_arbitrary:
        raise ValueError(f"unsupported standard baud rate: {baud_rate}")

    file_descriptor = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    original_settings = None
    try:
        original_settings = termios.tcgetattr(file_descriptor)
        settings = termios.tcgetattr(file_descriptor)
        settings[0] = 0
        settings[1] = 0
        settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        settings[3] = 0
        if baud_rate in BAUD_RATES:
            settings[4] = BAUD_RATES[baud_rate]
            settings[5] = BAUD_RATES[baud_rate]
        settings[6][termios.VMIN] = 0
        settings[6][termios.VTIME] = 0
        termios.tcsetattr(file_descriptor, termios.TCSANOW, settings)

        if baud_rate not in BAUD_RATES:
            raw = bytearray(44)
            fcntl.ioctl(file_descriptor, TCGETS2, raw, True)
            cflag = int.from_bytes(raw[8:12], "little")
            raw[8:12] = ((cflag & ~CBAUD) | BOTHER).to_bytes(4, "little")
            raw[36:40] = baud_rate.to_bytes(4, "little")
            raw[40:44] = baud_rate.to_bytes(4, "little")
            fcntl.ioctl(file_descriptor, TCSETS2, raw)

        verify_line_speed(file_descriptor, baud_rate)
        termios.tcflush(file_descriptor, termios.TCIOFLUSH)
        return file_descriptor, original_settings
    except BaseException:
        try:
            if original_settings is not None:
                termios.tcsetattr(file_descriptor, termios.TCSANOW, original_settings)
        finally:
            os.close(file_descriptor)
        raise

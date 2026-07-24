#!/usr/bin/env python3
"""Verify bidirectional traffic through one PicoUart CDC-to-UART bridge."""

import argparse
import os
import secrets
import select
import sys
import termios
import time


BAUD_RATES = {
    9600: termios.B9600,
    19200: termios.B19200,
    38400: termios.B38400,
    57600: termios.B57600,
    115200: termios.B115200,
}


def configure_port(path: str, baud_rate: int) -> tuple[int, list]:
    file_descriptor = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    original_settings = termios.tcgetattr(file_descriptor)
    settings = termios.tcgetattr(file_descriptor)

    settings[0] = 0
    settings[1] = 0
    settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    settings[3] = 0
    settings[4] = BAUD_RATES[baud_rate]
    settings[5] = BAUD_RATES[baud_rate]
    settings[6][termios.VMIN] = 0
    settings[6][termios.VTIME] = 0
    termios.tcsetattr(file_descriptor, termios.TCSANOW, settings)
    termios.tcflush(file_descriptor, termios.TCIOFLUSH)
    return file_descriptor, original_settings


def write_all(file_descriptor: int, data: bytes) -> None:
    offset = 0
    while offset < len(data):
        try:
            count = os.write(file_descriptor, data[offset:])
        except BlockingIOError:
            select.select([], [file_descriptor], [], 0.1)
            continue
        offset += count
    termios.tcdrain(file_descriptor)


def wait_for_marker(file_descriptor: int, marker: bytes, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    received = bytearray()

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        readable, _, _ = select.select([file_descriptor], [], [], remaining)
        if not readable:
            continue

        try:
            received.extend(os.read(file_descriptor, 4096))
        except BlockingIOError:
            continue

        if marker in received:
            return True
        if len(received) > 8192:
            del received[:-4096]

    return False


def test_direction(source_fd: int,
                   destination_fd: int,
                   direction: str,
                   payload_size: int,
                   timeout: float) -> bool:
    marker = (
        f"PICO_UART_TEST:{direction}:{secrets.token_hex(12)}:".encode("ascii")
        + secrets.token_bytes(payload_size)
    )
    termios.tcflush(destination_fd, termios.TCIFLUSH)
    write_all(source_fd, marker)

    if wait_for_marker(destination_fd, marker, timeout):
        print(f"PASS {direction}: {len(marker)} bytes")
        return True

    print(f"FAIL {direction}: marker was not received within {timeout:.1f}s", file=sys.stderr)
    return False


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify both directions of one PicoUart CDC-to-UART link."
    )
    parser.add_argument("--pico-port", required=True, help="PicoUart CDC device, for example /dev/ttyACM2")
    parser.add_argument("--peer-port", required=True, help="Connected UART peer device, for example /dev/serial0")
    parser.add_argument("--label", default="bridge", help="Name shown in test output")
    parser.add_argument("--baud", type=int, default=115200, choices=BAUD_RATES)
    parser.add_argument("--payload-bytes", type=int, default=64)
    parser.add_argument("--timeout", type=float, default=3.0)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.payload_bytes < 1 or arguments.payload_bytes > 4096:
        print("--payload-bytes must be between 1 and 4096", file=sys.stderr)
        return 2
    if arguments.timeout <= 0:
        print("--timeout must be greater than zero", file=sys.stderr)
        return 2

    pico_fd = -1
    peer_fd = -1
    pico_settings = None
    peer_settings = None

    try:
        pico_fd, pico_settings = configure_port(arguments.pico_port, arguments.baud)
        peer_fd, peer_settings = configure_port(arguments.peer_port, arguments.baud)
        print(f"Testing {arguments.label} at {arguments.baud} baud")
        pico_to_peer = test_direction(pico_fd,
                                      peer_fd,
                                      "pico-to-peer",
                                      arguments.payload_bytes,
                                      arguments.timeout)
        peer_to_pico = test_direction(peer_fd,
                                      pico_fd,
                                      "peer-to-pico",
                                      arguments.payload_bytes,
                                      arguments.timeout)
        return 0 if pico_to_peer and peer_to_pico else 1
    except OSError as error:
        print(f"Serial setup failed: {error}", file=sys.stderr)
        return 2
    finally:
        if pico_fd >= 0 and pico_settings is not None:
            termios.tcsetattr(pico_fd, termios.TCSANOW, pico_settings)
            os.close(pico_fd)
        if peer_fd >= 0 and peer_settings is not None:
            termios.tcsetattr(peer_fd, termios.TCSANOW, peer_settings)
            os.close(peer_fd)


if __name__ == "__main__":
    sys.exit(main())
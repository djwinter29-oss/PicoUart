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
    230400: termios.B230400,
    460800: termios.B460800,
    921600: termios.B921600,
    1000000: termios.B1000000,
}
STANDARD_BAUD_RATES = tuple(BAUD_RATES)


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
    peer_group = parser.add_mutually_exclusive_group(required=True)
    peer_group.add_argument("--peer-port", help="Connected UART peer device, for example /dev/serial0")
    peer_group.add_argument("--loopback", action="store_true", help="Test a TX-to-RX jumper on the PicoUart channel")
    parser.add_argument("--label", default="bridge", help="Name shown in test output")
    baud_group = parser.add_mutually_exclusive_group()
    baud_group.add_argument("--baud", type=int, choices=BAUD_RATES, help="One baud rate; defaults to 115200")
    baud_group.add_argument("--all-baud-rates", action="store_true", help="Test every supported standard baud rate")
    parser.add_argument("--payload-bytes", type=int, default=64)
    parser.add_argument("--timeout", type=float, default=3.0)
    return parser.parse_args()


def run_test(arguments: argparse.Namespace, baud_rate: int) -> int:
    pico_fd = -1
    peer_fd = -1
    pico_settings = None
    peer_settings = None

    try:
        pico_fd, pico_settings = configure_port(arguments.pico_port, baud_rate)
        if arguments.loopback:
            time.sleep(0.05)
            print(f"Testing {arguments.label} loopback at {baud_rate} baud")
            passed = test_direction(pico_fd,
                                    pico_fd,
                                    "pico-loopback",
                                    arguments.payload_bytes,
                                    arguments.timeout)
            return 0 if passed else 1

        peer_fd, peer_settings = configure_port(arguments.peer_port, baud_rate)
        time.sleep(0.05)
        print(f"Testing {arguments.label} at {baud_rate} baud")
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


def main() -> int:
    arguments = parse_arguments()
    if arguments.payload_bytes < 1 or arguments.payload_bytes > 4096:
        print("--payload-bytes must be between 1 and 4096", file=sys.stderr)
        return 2
    if arguments.timeout <= 0:
        print("--timeout must be greater than zero", file=sys.stderr)
        return 2

    baud_rates = STANDARD_BAUD_RATES if arguments.all_baud_rates else (arguments.baud or 115200,)
    result = 0
    for baud_rate in baud_rates:
        result |= run_test(arguments, baud_rate)
    return result


if __name__ == "__main__":
    sys.exit(main())
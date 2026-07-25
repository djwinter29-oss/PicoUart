#!/usr/bin/env python3
"""Stress PicoUart UART0, UART2/UART3, and UART5 concurrently."""

import argparse
import os
import select
import sys
import termios
import threading
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
DEFAULT_RATES = tuple(BAUD_RATES)


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


def write_all(file_descriptor: int, data: bytes, deadline: float) -> None:
    offset = 0
    while offset < len(data):
        if time.monotonic() >= deadline:
            raise TimeoutError("write timed out")
        try:
            count = os.write(file_descriptor, data[offset:])
        except BlockingIOError:
            select.select([], [file_descriptor], [], min(0.1, deadline - time.monotonic()))
            continue
        if count == 0:
            raise OSError("serial write returned zero bytes")
        offset += count


def read_exact(file_descriptor: int, expected: bytes, deadline: float) -> None:
    received = bytearray()
    while len(received) < len(expected):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"received {len(received)} of {len(expected)} bytes")
        readable, _, _ = select.select([file_descriptor], [], [], remaining)
        if not readable:
            continue
        try:
            chunk = os.read(file_descriptor, len(expected) - len(received))
        except BlockingIOError:
            continue
        if chunk:
            received.extend(chunk)

    if received != expected:
        raise ValueError("received data did not match transmitted data")


def payload_for(label: str, sequence: int, size: int) -> bytes:
    prefix = f"PICO_UART_BENCH:{label}:{sequence:08x}:".encode("ascii")
    if len(prefix) >= size:
        return prefix[:size]
    pattern = bytes(range(256))
    payload = bytearray(prefix)
    while len(payload) < size:
        payload.extend(pattern[:size - len(payload)])
    return bytes(payload)


def run_stream(label: str,
               source_fd: int,
               destination_fd: int,
               duration: float,
               payload_bytes: int,
               timeout: float,
               start: threading.Barrier,
               result: dict) -> None:
    bytes_verified = 0
    sequence = 0

    try:
        start.wait()
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            payload = payload_for(label, sequence, payload_bytes)
            write_all(source_fd, payload, time.monotonic() + timeout)
            read_exact(destination_fd, payload, time.monotonic() + timeout)
            bytes_verified += len(payload)
            sequence += 1
        result[label] = (bytes_verified, None)
    except (OSError, TimeoutError, ValueError, threading.BrokenBarrierError) as error:
        result[label] = (bytes_verified, str(error))


def parse_rates(value: str) -> tuple[int, ...]:
    try:
        rates = tuple(int(item) for item in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("--rates must be comma-separated integers") from error
    if not rates or any(rate not in BAUD_RATES for rate in rates):
        raise argparse.ArgumentTypeError("--rates contains an unsupported baud rate")
    return rates


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Benchmark all configured PicoUart test links concurrently."
    )
    parser.add_argument("--uart0-pico", required=True, help="PicoUart CDC0 device")
    parser.add_argument("--uart0-peer", required=True, help="Debug Probe UART device")
    parser.add_argument("--uart2", required=True, help="PicoUart CDC2 device")
    parser.add_argument("--uart3", required=True, help="PicoUart CDC3 device")
    parser.add_argument("--uart5", required=True, help="PicoUart CDC5 device")
    parser.add_argument("--uart0-baud", type=int, default=115200, choices=BAUD_RATES,
                        help="UART0 and Debug Probe rate; defaults to 115200")
    parser.add_argument("--rates", type=parse_rates, default=DEFAULT_RATES,
                        help="PIO rates to test, comma-separated")
    parser.add_argument("--duration", type=float, default=10.0,
                        help="Transmit duration per PIO rate in seconds")
    parser.add_argument("--payload-bytes", type=int, default=1024,
                        help="Bytes per verified stream block")
    parser.add_argument("--timeout", type=float, default=3.0,
                        help="Timeout for one block transfer in seconds")
    return parser.parse_args()


def close_port(file_descriptor: int, settings: list) -> None:
    termios.tcsetattr(file_descriptor, termios.TCSANOW, settings)
    os.close(file_descriptor)


def benchmark_rate(arguments: argparse.Namespace, pio_baud: int) -> bool:
    ports: list[tuple[int, list]] = []
    results: dict[str, tuple[int, str | None]] = {}

    try:
        uart0_pico, uart0_pico_settings = configure_port(arguments.uart0_pico, arguments.uart0_baud)
        ports.append((uart0_pico, uart0_pico_settings))
        uart0_peer, uart0_peer_settings = configure_port(arguments.uart0_peer, arguments.uart0_baud)
        ports.append((uart0_peer, uart0_peer_settings))
        uart2, uart2_settings = configure_port(arguments.uart2, pio_baud)
        ports.append((uart2, uart2_settings))
        uart3, uart3_settings = configure_port(arguments.uart3, pio_baud)
        ports.append((uart3, uart3_settings))
        uart5, uart5_settings = configure_port(arguments.uart5, pio_baud)
        ports.append((uart5, uart5_settings))

        time.sleep(0.1)
        start = threading.Barrier(5)
        streams = (
            ("uart0-pico-to-peer", uart0_pico, uart0_peer),
            ("uart0-peer-to-pico", uart0_peer, uart0_pico),
            ("uart2-to-uart3", uart2, uart3),
            ("uart3-to-uart2", uart3, uart2),
            ("uart5-loopback", uart5, uart5),
        )
        threads = [
            threading.Thread(target=run_stream,
                             args=(label, source_fd, destination_fd, arguments.duration,
                                   arguments.payload_bytes, arguments.timeout, start, results))
            for label, source_fd, destination_fd in streams
        ]

        print(f"Benchmarking PIO at {pio_baud} baud; UART0 at {arguments.uart0_baud} baud")
        started = time.monotonic()
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        elapsed = time.monotonic() - started

        passed = True
        for label, _, _ in streams:
            bytes_verified, error = results.get(label, (0, "stream did not report a result"))
            throughput = bytes_verified / elapsed if elapsed > 0 else 0.0
            if error is None:
                print(f"PASS {label}: {bytes_verified} bytes, {throughput:.1f} B/s")
            else:
                print(f"FAIL {label}: {bytes_verified} bytes, {error}", file=sys.stderr)
                passed = False
        return passed
    except OSError as error:
        print(f"Serial setup failed: {error}", file=sys.stderr)
        return False
    finally:
        for file_descriptor, settings in reversed(ports):
            close_port(file_descriptor, settings)


def main() -> int:
    arguments = parse_arguments()
    if arguments.duration <= 0:
        print("--duration must be greater than zero", file=sys.stderr)
        return 2
    if arguments.payload_bytes < 32 or arguments.payload_bytes > 4096:
        print("--payload-bytes must be between 32 and 4096", file=sys.stderr)
        return 2
    if arguments.timeout <= 0:
        print("--timeout must be greater than zero", file=sys.stderr)
        return 2

    passed = True
    for rate in arguments.rates:
        passed = benchmark_rate(arguments, rate) and passed
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
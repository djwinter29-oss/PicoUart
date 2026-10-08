#!/usr/bin/env python3
"""Stress all six ports using two crossed pairs and two loopback channels."""

import argparse
import math
import os
import select
import struct
import sys
import termios
import threading
import time
from datetime import datetime, timezone
from .config import BAUD_RATES, configure_port

DEFAULT_RATES = tuple(BAUD_RATES)
LINE_CODING_SETTLE_SECONDS = 8.0


def write_all(file_descriptor: int, data: bytes, deadline: float) -> None:
    offset = 0
    while offset < len(data):
        if time.monotonic() >= deadline:
            raise TimeoutError("write timed out")
        try:
            count = os.write(file_descriptor, data[offset:])
        except BlockingIOError:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("write timed out")
            select.select([], [file_descriptor], [], min(0.1, remaining))
            continue
        if count == 0:
            raise OSError("serial write returned zero bytes")
        offset += count
        if time.monotonic() >= deadline:
            raise TimeoutError("write timed out")


def read_exact(file_descriptor: int, expected: bytes, deadline: float, timing: dict | None = None) -> None:
    received = bytearray()
    while len(received) < len(expected):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"received {len(received)} of {len(expected)} bytes")
        readable, _, _ = select.select([file_descriptor], [], [], remaining)
        if time.monotonic() >= deadline:
            raise TimeoutError(f"received {len(received)} of {len(expected)} bytes")
        if not readable:
            continue
        try:
            chunk = os.read(file_descriptor, len(expected) - len(received))
        except BlockingIOError:
            continue
        if chunk:
            received_at = time.monotonic()
            if timing is not None and "first_receive_monotonic" not in timing:
                timing["first_receive_utc"] = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
                timing["first_receive_monotonic"] = received_at
            received.extend(chunk)
            if received_at >= deadline:
                raise TimeoutError(f"received {len(received)} of {len(expected)} bytes after deadline")

    if received != expected:
        raise ValueError("received data did not match transmitted data")


def payload_for(label: str, sequence: int, size: int) -> bytes:
    prefix = b"PU:" + label.encode("ascii")[:8].ljust(8, b"_") + struct.pack(">Q", sequence)
    if len(prefix) >= size:
        return prefix[:size]
    pattern = bytes(range(256))
    payload = bytearray(prefix)
    while len(payload) < size:
        payload.extend(pattern[: size - len(payload)])
    return bytes(payload)


def run_stream(
    label: str,
    source_fd: int,
    destination_fd: int,
    duration: float,
    payload_bytes: int,
    timeout: float,
    start: threading.Barrier,
    result: dict,
    timing: dict,
) -> None:
    bytes_verified = 0

    def stamp() -> str:
        return datetime.now(timezone.utc).isoformat(timespec="milliseconds")

    timing[label] = {"thread_start_utc": stamp(), "thread_start_monotonic": time.monotonic()}
    sequence = 0

    try:
        start.wait()
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            payload = payload_for(label, sequence, payload_bytes)
            if sequence == 0:
                timing[label]["first_send_utc"] = stamp()
                timing[label]["first_send_monotonic"] = time.monotonic()
            write_all(source_fd, payload, time.monotonic() + timeout)
            read_exact(destination_fd, payload, time.monotonic() + timeout, timing[label])
            bytes_verified += len(payload)
            sequence += 1
        if bytes_verified == 0:
            result[label] = (0, "stream completed without verifying a payload")
        else:
            result[label] = (bytes_verified, None)
    except (OSError, TimeoutError, ValueError, threading.BrokenBarrierError) as error:
        result[label] = (bytes_verified, str(error))


def parse_rates(value: str) -> tuple[int, ...]:
    try:
        rates = tuple(int(item) for item in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("--rates must be comma-separated integers") from error
    if not rates or any(rate <= 0 for rate in rates):
        raise argparse.ArgumentTypeError("--rates must contain positive baud rates")
    return rates


def incremental_rates(start_rate: int, rate_step: int, max_rate: int) -> tuple[int, ...]:
    if start_rate <= 0 or rate_step <= 0 or max_rate < start_rate:
        raise ValueError("incremental start and step must be positive, and max rate must be >= start rate")
    return tuple(range(start_rate, max_rate + 1, rate_step))


def build_parser(add_help: bool = True) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Benchmark the fixed six-port PicoUart HIL fixture concurrently.", add_help=add_help
    )
    for channel in range(6):
        parser.add_argument(f"--cdc{channel}", required=True, help=f"PicoUart CDC{channel} device")
    rate_group = parser.add_mutually_exclusive_group()
    rate_group.add_argument("--rates", type=parse_rates, help="rates to test, comma-separated")
    rate_group.add_argument(
        "--incremental",
        action="store_true",
        help="increase the rate until the first failed six-stream run",
    )
    parser.set_defaults(rates=DEFAULT_RATES)
    parser.add_argument("--incremental-start-rate", type=int, default=460800)
    parser.add_argument("--incremental-rate-step", type=int, default=100000)
    parser.add_argument("--incremental-max-rate", type=int, default=3000000)
    parser.add_argument("--duration", type=float, default=10.0, help="Transmit duration per rate in seconds")
    parser.add_argument("--payload-bytes", type=int, default=1024, help="Bytes per verified stream block")
    parser.add_argument(
        "--timeout",
        type=float,
        default=3.0,
        help="Timeout for each block write/read phase; an in-flight block may finish after duration",
    )
    parser.add_argument("--settle-seconds", type=float, default=8.0, help="wait after configuring all ports")
    parser.add_argument(
        "--setup-only", action="store_true", help="configure ports and settle, but do not transmit data"
    )
    return parser


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    return build_parser().parse_args(argv)


def close_port(file_descriptor: int, settings: list) -> None:
    try:
        termios.tcsetattr(file_descriptor, termios.TCSANOW, settings)
    finally:
        os.close(file_descriptor)


def close_ports(ports: list[tuple[int, list]]) -> OSError | None:
    """Close every configured port and return the first terminal cleanup error."""
    first_error = None
    for file_descriptor, settings in reversed(ports):
        try:
            close_port(file_descriptor, settings)
        except OSError as error:
            if first_error is None:
                first_error = error
    return first_error


def fixture_paths_valid(arguments: argparse.Namespace) -> bool:
    """Reject endpoint aliases that would invalidate the fixed six-port fixture."""
    endpoints = [(f"--cdc{channel}", getattr(arguments, f"cdc{channel}")) for channel in range(6)]
    seen: dict[str, str] = {}
    for name, path in endpoints:
        resolved = os.path.realpath(path)
        if resolved in seen:
            print(f"{name} resolves to the same endpoint as {seen[resolved]}", file=sys.stderr)
            return False
        seen[resolved] = name

    return True


def benchmark_rate(arguments: argparse.Namespace, stream_baud: int) -> bool:
    ports: list[tuple[int, list]] = []
    results: dict[str, tuple[int, str | None]] = {}
    passed = False

    if not fixture_paths_valid(arguments):
        return False

    try:
        descriptors = {}
        for channel in range(6):
            descriptor, settings = configure_port(
                getattr(arguments, f"cdc{channel}"), stream_baud, allow_arbitrary=True
            )
            ports.append((descriptor, settings))
            descriptors[channel] = descriptor

        streams: list[tuple[str, int, int]] = [
            ("cdc0-to-cdc2", descriptors[0], descriptors[2]),
            ("cdc2-to-cdc0", descriptors[2], descriptors[0]),
            ("cdc3-to-cdc4", descriptors[3], descriptors[4]),
            ("cdc4-to-cdc3", descriptors[4], descriptors[3]),
            ("cdc1-loopback", descriptors[1], descriptors[1]),
            ("cdc5-loopback", descriptors[5], descriptors[5]),
        ]

        time.sleep(getattr(arguments, "settle_seconds", LINE_CODING_SETTLE_SECONDS))
        if getattr(arguments, "setup_only", False):
            print(f"SETUP PASS at {stream_baud} baud")
            passed = True
        else:
            start = threading.Barrier(len(streams))
            timing: dict[str, dict] = {}
            threads = [
                threading.Thread(
                    target=run_stream,
                    args=(
                        label,
                        source_fd,
                        destination_fd,
                        arguments.duration,
                        arguments.payload_bytes,
                        arguments.timeout,
                        start,
                        results,
                        timing,
                    ),
                )
                for label, source_fd, destination_fd in streams
            ]

            print(f"Benchmarking all six HIL fixture streams at {stream_baud} baud")
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
            for label, timestamps in timing.items():
                print(f"TIME {label}: {timestamps}")
    except OSError as error:
        print(f"Serial setup failed: {error}", file=sys.stderr)
        passed = False
    finally:
        cleanup_error = close_ports(ports)
        if cleanup_error is not None:
            print(f"Serial cleanup failed: {cleanup_error}", file=sys.stderr)
            passed = False
    return passed


def main(arguments: argparse.Namespace | None = None) -> int:
    if arguments is None:
        arguments = parse_arguments()
    if not math.isfinite(arguments.duration) or arguments.duration <= 0:
        print("--duration must be greater than zero", file=sys.stderr)
        return 2
    if arguments.payload_bytes < 32 or arguments.payload_bytes > 4096:
        print("--payload-bytes must be between 32 and 4096", file=sys.stderr)
        return 2
    if not math.isfinite(arguments.timeout) or arguments.timeout <= 0:
        print("--timeout must be greater than zero", file=sys.stderr)
        return 2
    if not math.isfinite(arguments.settle_seconds) or arguments.settle_seconds < 0:
        print("--settle-seconds must be finite and >= 0", file=sys.stderr)
        return 2

    try:
        rates = (
            incremental_rates(
                arguments.incremental_start_rate,
                arguments.incremental_rate_step,
                arguments.incremental_max_rate,
            )
            if arguments.incremental
            else arguments.rates
        )
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 2

    passed = True
    highest_passing_rate = None
    first_failing_rate = None
    for rate in rates:
        if arguments.incremental:
            print(f"Incremental rate: {rate} baud")
        rate_passed = benchmark_rate(arguments, rate)
        passed = rate_passed and passed
        if arguments.incremental:
            print(f"Incremental result: {rate} baud {'PASS' if rate_passed else 'FAIL'}")
            if rate_passed:
                highest_passing_rate = rate
            else:
                first_failing_rate = rate
                break
    if arguments.incremental:
        highest = f"{highest_passing_rate} baud" if highest_passing_rate is not None else "none"
        failed = f"{first_failing_rate} baud" if first_failing_rate is not None else "not reached"
        print(f"Incremental summary: highest passing rate={highest}; first failing rate={failed}")
        return 0 if highest_passing_rate is not None else 1
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())

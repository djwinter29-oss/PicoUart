#!/usr/bin/env python3
"""Run a clean, synchronized full-duplex test on one CDC pair."""

import argparse
import math
import serial
import sys
import threading
import time

from .config import verify_line_speed

PICO = "/dev/serial/by-id/usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C"
PAIRS = {
    "stage1": ("-if00", "-if04"),
    "stage2": ("-if06", "-if08"),
}


def configure(path: str, baud: int):
    port = serial.Serial(path, baud, bytesize=8, parity="N", stopbits=1, timeout=1, write_timeout=3)
    try:
        port.reset_input_buffer()
        port.reset_output_buffer()
        verify_line_speed(port.fileno(), baud)
        return port
    except Exception:
        port.close()
        raise


def synchronize(a, b, settle: float, direction: str) -> None:
    """Wait for deferred line coding and verify only the requested direction(s)."""
    time.sleep(settle)
    for attempt in range(3):
        a.reset_input_buffer()
        a.reset_output_buffer()
        b.reset_input_buffer()
        b.reset_output_buffer()
        if direction in ("both", "a-to-b"):
            a.write(b"SYNC-A->B")
            a.flush()
            if b.read(9) != b"SYNC-A->B":
                continue
        if direction in ("both", "b-to-a"):
            b.write(b"SYNC-B->A")
            b.flush()
            if a.read(9) != b"SYNC-B->A":
                continue
        a.reset_input_buffer()
        a.reset_output_buffer()
        b.reset_input_buffer()
        b.reset_output_buffer()
        time.sleep(0.5)
        return
    raise RuntimeError(f"{direction} synchronization failed after 3 attempts")


def run(
    rate: int, suffixes: tuple[str, str], duration: float, settle: float, payload_size: int, direction: str
) -> tuple[bool, dict[str, tuple[int, str | None]]]:
    a = configure(PICO + suffixes[0], rate)
    b = None
    results: dict[str, tuple[int, str | None]] = {}
    labels = ("a-to-b", "b-to-a") if direction == "both" else (direction,)
    barrier = threading.Barrier(len(labels))
    try:
        b = configure(PICO + suffixes[1], rate)
        synchronize(a, b, settle, direction)

        def flow(label, source, destination):
            try:
                barrier.wait()
                deadline = time.monotonic() + duration
                count = 0
                while time.monotonic() < deadline:
                    payload = (
                        label.encode()
                        + b":"
                        + str(count).zfill(10).encode()
                        + b":"
                        + bytes(i % 251 for i in range(payload_size))
                    )[:payload_size]
                    source.write(payload)
                    source.flush()
                    received = bytearray()
                    frame_deadline = time.monotonic() + 3
                    while len(received) < len(payload) and time.monotonic() < frame_deadline:
                        chunk = destination.read(len(payload) - len(received))
                        if chunk:
                            received.extend(chunk)
                    if bytes(received) != payload:
                        raise RuntimeError(f"received {len(received)}/{len(payload)}")
                    count += 1
                if count == 0:
                    raise RuntimeError("stream completed without verifying a payload")
                results[label] = (count * payload_size, None)
            except Exception as exc:
                results[label] = (0, repr(exc))

        threads = []
        if direction in ("both", "a-to-b"):
            threads.append(threading.Thread(target=flow, args=("a-to-b", a, b)))
        if direction in ("both", "b-to-a"):
            threads.append(threading.Thread(target=flow, args=("b-to-a", b, a)))
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
    finally:
        try:
            a.close()
        finally:
            if b is not None:
                b.close()
    passed = all(label in results and results[label][0] > 0 and results[label][1] is None for label in labels)
    return passed, results


def parse_rates(value: str) -> tuple[int, ...]:
    try:
        rates = tuple(int(item) for item in value.split(","))
    except ValueError as error:
        raise argparse.ArgumentTypeError("--rates must be comma-separated integers") from error
    if not rates or any(rate <= 0 for rate in rates):
        raise argparse.ArgumentTypeError("--rates must contain positive baud rates")
    return rates


def build_parser(add_help: bool = True) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, add_help=add_help)
    parser.add_argument("pair", choices=PAIRS)
    parser.add_argument("--rates", required=True, type=parse_rates, help="comma-separated baud rates")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--settle", type=float, default=8.0)
    parser.add_argument("--payload", type=int, default=1024)
    parser.add_argument("--direction", choices=("both", "a-to-b", "b-to-a"), default="both")
    return parser


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    return build_parser().parse_args(argv)


def main(arguments: argparse.Namespace | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args() if arguments is None else arguments
    if args.runs < 1:
        parser.error("--runs must be greater than zero")
    if not math.isfinite(args.duration) or args.duration <= 0:
        parser.error("--duration must be finite and greater than zero")
    if not math.isfinite(args.settle) or args.settle < 0:
        parser.error("--settle must be finite and >= 0")
    if args.payload < 1:
        parser.error("--payload must be greater than zero")
    suffixes = PAIRS[args.pair]
    exit_code = 0
    for rate in args.rates:
        for run_number in range(1, args.runs + 1):
            try:
                passed, results = run(rate, suffixes, args.duration, args.settle, args.payload, args.direction)
                print(f"{args.pair} rate={rate} run={run_number} {'PASS' if passed else 'FAIL'} {results}", flush=True)
                if not passed:
                    exit_code = max(exit_code, 1)
            except Exception as exc:
                print(f"{args.pair} rate={rate} run={run_number} SETUP_FAIL {exc!r}", flush=True)
                exit_code = 2
    return exit_code


if __name__ == "__main__":
    sys.exit(main())

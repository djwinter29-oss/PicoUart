#!/usr/bin/env python3
"""Run a clean, synchronized full-duplex test on one CDC pair."""

import argparse
import serial
import sys
import threading
import time

PICO = "/dev/serial/by-id/usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C"
PAIRS = {
    "stage2": ("-if02", "-if04"),
    "stage3": ("-if06", "-if08"),
}


def configure(path: str, baud: int):
    port = serial.Serial(path, baud, bytesize=8, parity="N", stopbits=1,
                         timeout=1, write_timeout=3)
    port.reset_input_buffer()
    port.reset_output_buffer()
    return port


def synchronize(a, b, settle: float, direction: str) -> None:
    """Wait for deferred line coding and verify only the requested direction(s)."""
    time.sleep(settle)
    for attempt in range(3):
        a.reset_input_buffer(); a.reset_output_buffer()
        b.reset_input_buffer(); b.reset_output_buffer()
        if direction in ("both", "a-to-b"):
            a.write(b"SYNC-A->B"); a.flush()
            if b.read(9) != b"SYNC-A->B":
                continue
        if direction in ("both", "b-to-a"):
            b.write(b"SYNC-B->A"); b.flush()
            if a.read(9) != b"SYNC-B->A":
                continue
        a.reset_input_buffer(); a.reset_output_buffer()
        b.reset_input_buffer(); b.reset_output_buffer()
        time.sleep(0.5)
        return
    raise RuntimeError(f"{direction} synchronization failed after 3 attempts")


def run(rate: int, suffixes: tuple[str, str], duration: float, settle: float,
        payload_size: int, direction: str) -> tuple[bool, dict[str, tuple[int, str | None]]]:
    a = configure(PICO + suffixes[0], rate)
    b = configure(PICO + suffixes[1], rate)
    results: dict[str, tuple[int, str | None]] = {}
    barrier = threading.Barrier(2 if direction == "both" else 1)
    try:
        synchronize(a, b, settle, direction)

        def flow(label, source, destination):
            try:
                barrier.wait()
                deadline = time.monotonic() + duration
                count = 0
                while time.monotonic() < deadline:
                    payload = (label.encode() + b":" + str(count).zfill(10).encode() + b":" +
                               bytes(i % 251 for i in range(payload_size)))[:payload_size]
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
        a.close()
        b.close()
    return all(error is None for _, error in results.values()), results


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("pair", choices=PAIRS)
    parser.add_argument("--rates", required=True, help="comma-separated baud rates")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--settle", type=float, default=8.0)
    parser.add_argument("--payload", type=int, default=1024)
    parser.add_argument("--direction", choices=("both", "a-to-b", "b-to-a"), default="both")
    args = parser.parse_args()
    suffixes = PAIRS[args.pair]
    for rate_text in args.rates.split(","):
        rate = int(rate_text)
        for run_number in range(1, args.runs + 1):
            try:
                passed, results = run(rate, suffixes, args.duration, args.settle, args.payload,
                                     args.direction)
                print(f"{args.pair} rate={rate} run={run_number} "
                      f"{'PASS' if passed else 'FAIL'} {results}", flush=True)
            except Exception as exc:
                print(f"{args.pair} rate={rate} run={run_number} SETUP_FAIL {exc!r}",
                      flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

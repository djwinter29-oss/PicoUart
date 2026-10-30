# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Observe selected-board startup with retained failures and an optional identified CDC1 loopback probe."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

from pico_uart import PicoUartHid


def build_parser(add_help: bool = True) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, add_help=add_help)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--event", choices=("cold-power-on", "swd-reset", "watchdog", "usb-reset"), required=True)
    parser.add_argument("--artifact", required=True, type=Path, help="Previously flashed ELF (hashed, not flashed)")
    parser.add_argument("--output", required=True, type=Path, help="New JSON evidence file; never overwritten")
    parser.add_argument("--timeout", type=float, default=20, help="Observation deadline, seconds")
    parser.add_argument("--interval", type=float, default=0.2, help="Seconds between failed attempts")
    parser.add_argument("--stable-seconds", type=float, default=12, help="Required closed-state observation window")
    parser.add_argument("--cdc1", help="Opt in to CDC1 open/readiness/payload verification on the fixed loopback fixture")
    parser.add_argument("--sample", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--probe", action="store_true", help=argparse.SUPPRESS)
    return parser


def probe_channel(serial: str, path: str) -> dict:
    """Verify worker startup on the identified loopback; never open an unrelated serial device."""
    from ..serial.config import configure_port, pico_uart_endpoint
    from ..serial.stress import close_ports, read_exact, write_all

    if pico_uart_endpoint(path) != (serial, 1):
        raise ValueError(f"{path} is not CDC1 on PicoUart {serial}")
    ports = []
    try:
        ports.append(configure_port(path, 115200))
        payload = b"PicoUart startup loopback:" + bytes(range(128))
        descriptor = ports[0][0]
        write_all(descriptor, payload, time.monotonic() + 2)
        read_exact(descriptor, payload, time.monotonic() + 2)
        return {"channel": 1, "baud": 115200, "verified_bytes": len(payload)}
    finally:
        error = close_ports(ports)
        if error:
            raise error


def sample_board(serial: str) -> dict:
    """Take one un-retried observation; a subprocess bounds HID operations that may block in the OS."""
    with PicoUartHid(serial_number=serial) as board:
        return {
            "hardware": board.read_hardware_info(), "configuration": board.read_configuration(),
            "runtime": board.read_runtime(), "firmware_version": board.read_firmware_version(),
            "channels": [board.read_channel(index) for index in range(6)],
        }


def observe(arguments: argparse.Namespace) -> dict:
    started = time.monotonic()
    evidence = {
        "result": "FAIL", "started_utc": datetime.now(timezone.utc).isoformat(), "serial": arguments.serial,
        "event": arguments.event, "timeout_s": arguments.timeout,
        "stable_seconds": arguments.stable_seconds,
        "artifact": str(arguments.artifact),
        "artifact_sha256": hashlib.sha256(arguments.artifact.read_bytes()).hexdigest(), "attempts": [],
        "cdc1": arguments.cdc1,
    }
    first_ready = None
    first_uptime = None
    while time.monotonic() - started < arguments.timeout:
        attempt = {"start_s": time.monotonic() - started}
        probing = False
        try:
            command = [
                sys.executable, "-m", "hil_test_suite.workflows.startup", "--sample", "--serial", arguments.serial,
                "--event", arguments.event, "--artifact", str(arguments.artifact), "--output", str(arguments.output),
            ]
            completed = subprocess.run(
                command, capture_output=True, text=True, check=True,
                timeout=min(2.0, arguments.timeout - (time.monotonic() - started)),
            )
            sample = json.loads(completed.stdout)
            attempt["sample"] = sample
            attempt["sample_end_s"] = time.monotonic() - started
            clock = sample["hardware"]["system_clock_hz"]
            uptime = sample["runtime"]["uptime_ms"]
            ready = (
                isinstance(clock, int) and not isinstance(clock, bool) and clock > 0
                and type(uptime) is int and 0 <= uptime <= 0xFFFFFFFF
                and len(sample["channels"]) == 6
                and all(channel["state"] == "closed" for channel in sample["channels"])
            )
            if not ready:
                attempt["error"] = "board must report its clock and all six channels closed"
            elif first_ready is None and arguments.cdc1:
                probing = True
                probe_command = [argument if argument != "--sample" else "--probe" for argument in command]
                probe_command += ["--cdc1", arguments.cdc1]
                completed = subprocess.run(
                    probe_command, capture_output=True, text=True, check=True,
                    timeout=min(8.0, arguments.timeout - (time.monotonic() - started)),
                )
                attempt["probe"] = json.loads(completed.stdout)
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError) as error:
            attempt["error"] = str(error)
            if isinstance(error, (subprocess.CalledProcessError, subprocess.TimeoutExpired)):
                for field in ("stdout", "stderr"):
                    value = getattr(error, field)
                    attempt[field] = value.decode(errors="replace") if isinstance(value, bytes) else value
        attempt["end_s"] = time.monotonic() - started
        evidence["attempts"].append(attempt)
        if "error" in attempt:
            if probing:
                evidence["error"] = "CDC1 startup readiness/payload probe failed"
                break
            if first_ready is not None:
                evidence["error"] = (
                    "observation deadline expired before the stability window completed"
                    if attempt["end_s"] >= arguments.timeout
                    else "lost closed-state observation after initial readiness"
                )
                break
        elif attempt["end_s"] <= arguments.timeout:
            sample_end = attempt["sample_end_s"]
            if first_uptime is not None:
                advance = (uptime - first_uptime) & 0xFFFFFFFF
                elapsed_ms = (sample_end - first_ready) * 1000
                if abs(advance - elapsed_ms) > 1000:
                    evidence["error"] = "uptime discontinuity during startup observation"
                    break
            if first_ready is None:
                first_ready = sample_end
                first_uptime = uptime
                evidence["first_closed_observation_s"] = first_ready
            if sample_end - first_ready >= arguments.stable_seconds:
                evidence["result"] = "PASS"
                break
        remaining = arguments.timeout - (time.monotonic() - started)
        if remaining > 0:
            time.sleep(min(arguments.interval, remaining))
    if evidence["result"] != "PASS" and "error" not in evidence:
        evidence["error"] = "observation deadline expired before the stability window completed"
    return evidence


def main(arguments: argparse.Namespace | None = None) -> int:
    arguments = arguments or build_parser().parse_args()
    if arguments.sample:
        print(json.dumps(sample_board(arguments.serial)))
        return 0
    if arguments.probe:
        if not arguments.cdc1:
            raise ValueError("CDC1 probe requires --cdc1")
        print(json.dumps(probe_channel(arguments.serial, arguments.cdc1)))
        return 0
    if any(not math.isfinite(value) or value <= 0
           for value in (arguments.timeout, arguments.interval, arguments.stable_seconds)):
        raise ValueError("timeout, interval and stable-seconds must be finite and positive")
    if arguments.stable_seconds >= arguments.timeout:
        raise ValueError("stable-seconds must be less than timeout")
    with arguments.output.open("x") as output:
        evidence = observe(arguments)
        json.dump(evidence, output, indent=2)
        output.write("\n")
    print(json.dumps(evidence, indent=2))
    return 0 if evidence["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())

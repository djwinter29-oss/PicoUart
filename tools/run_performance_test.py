#!/usr/bin/env python3
"""Run the PicoUart concurrent performance benchmark and record its result."""

from __future__ import annotations

import argparse
import datetime as dt
import re
import shlex
import subprocess
import sys
import os
from pathlib import Path
from types import SimpleNamespace

from hardware_test_result import prepend_result, write_raw_log
from hardware_test_health import (collect_hid_health, health_evidence, health_is_clean,
                                  health_summary)
from hardware_test_result import artifact_metadata

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
DEFAULT_RESULTS_FILE = REPO_ROOT / "docs/tests/performance-test-results.md"
PASS_PATTERN = re.compile(
    r"^PASS (?P<label>[^:]+): (?P<bytes>[0-9]+) bytes, (?P<throughput>[0-9.]+) B/s$",
    re.MULTILINE,
)
FAIL_PATTERN = re.compile(r"^FAIL (?P<label>[^:]+): (?P<error>.+)$", re.MULTILINE)
RATE_PATTERN = re.compile(r"^Benchmarking .*? at (?P<rate>[0-9]+) baud", re.MULTILINE)


def build_command(arguments: SimpleNamespace) -> list[str]:
    """Build the documented serial_stress_benchmark invocation."""
    command = [
        sys.executable,
        str(SCRIPT_DIR / "serial_stress_benchmark.py"),
        "--uart0-pico", arguments.uart0_pico,
        "--uart0-peer", arguments.uart0_peer,
        "--uart2", arguments.uart2,
        "--uart3", arguments.uart3,
        "--uart5", arguments.uart5,
        "--uart0-baud", str(arguments.uart0_baud),
        "--rates", arguments.rates,
        "--duration", str(arguments.duration),
        "--payload-bytes", str(arguments.payload_bytes),
        "--timeout", str(arguments.timeout),
    ]
    if arguments.uart1:
        command.extend(["--uart1", arguments.uart1])
        command.extend(["--uart1-peer", arguments.uart1_peer or arguments.uart2])
    if arguments.uart4:
        command.extend(["--uart4", arguments.uart4])
        command.extend(["--uart4-peer", arguments.uart4_peer or arguments.uart3])
    return command


def peer_path_matches(peer: str | None, expected: str) -> bool:
    """Return whether an explicitly supplied peer names the expected endpoint."""
    return peer is None or os.path.realpath(peer) == os.path.realpath(expected)


def parse_benchmark_output(output: str) -> dict[str, tuple[str, str, str]]:
    """Return label -> (result, verified bytes, throughput) from benchmark output."""
    results = {
        match.group("label"): ("PASS", match.group("bytes"), match.group("throughput"))
        for match in PASS_PATTERN.finditer(output)
    }
    results.update({
        match.group("label"): ("FAIL", "-", match.group("error"))
        for match in FAIL_PATTERN.finditer(output)
    })
    return results


def parse_benchmark_output_by_rate(output: str) -> dict[tuple[int, str], tuple[str, str, str]]:
    """Parse benchmark results without collapsing repeated link labels across rates."""
    results: dict[tuple[int, str], tuple[str, str, str]] = {}
    current_rate: int | None = None
    for line in output.splitlines():
        rate_match = RATE_PATTERN.match(line)
        if rate_match:
            current_rate = int(rate_match.group("rate"))
            continue
        pass_match = PASS_PATTERN.match(line)
        fail_match = FAIL_PATTERN.match(line)
        if current_rate is None:
            continue
        if pass_match:
            results[(current_rate, pass_match.group("label"))] = (
                "PASS", pass_match.group("bytes"), pass_match.group("throughput"))
        elif fail_match:
            results[(current_rate, fail_match.group("label"))] = (
                "FAIL", "-", fail_match.group("error"))
    return results


def format_result_entry(arguments: SimpleNamespace,
                        timestamp: str,
                        result: int,
                        output: str,
                        health_before: dict | None = None,
                        health_after: dict | None = None,
                        raw_log: Path | None = None) -> str:
    parsed = parse_benchmark_output_by_rate(output)
    clean = result == 0 and health_is_clean(health_after, health_before)
    overall = "PASS" if clean and arguments.uart1 and arguments.uart4 else "PARTIAL" if clean else "FAIL"
    expected_labels = ["uart0-pico-to-peer", "uart0-peer-to-pico", "uart5-loopback"]
    if arguments.uart1:
        expected_labels.extend(["uart1-to-uart2", "uart2-to-uart1"])
    else:
        expected_labels.extend(["uart2-to-uart3", "uart3-to-uart2"])
    if arguments.uart4:
        expected_labels.extend(["uart3-to-uart4", "uart4-to-uart3"])

    lines = [
        f"## {timestamp} - {arguments.board} - Performance Test",
        "",
        f"**Result:** `{overall}`",
        f"**Firmware:** {arguments.firmware_version}, `{arguments.firmware_commit}`",
        f"**Board:** `{arguments.board}`",
        f"**Test date/time:** `{timestamp}`",
        "**Wiring:** Performance benchmark fixture",
        "**RTS/CTS:** disabled",
        "",
        "### Configuration",
        "",
        f"- Baud rates: {arguments.rates}",
        f"- UART0 baud: {arguments.uart0_baud}",
        f"- Duration per rate: {arguments.duration} seconds",
        f"- Payload: {arguments.payload_bytes} bytes",
        f"- Artifact: {getattr(arguments, 'artifact_path', 'not supplied')}",
        f"- Artifact SHA-256: `{getattr(arguments, 'artifact_sha256', 'not supplied')}`",
        f"- HID firmware version: `{health_after.get('firmware_version', 'unknown') if health_after else 'unknown'}`",
        "",
        "### Results",
        "",
        "| Rate | Link | Result | Verified bytes | Throughput / error |",
        "| ---: | --- | --- | ---: | --- |",
    ]
    for rate in (int(item) for item in arguments.rates.split(",")):
        for label in expected_labels:
            status, verified, throughput = parsed.get(
                (rate, label), ("NOT REPORTED", "-", "-"))
            lines.append(f"| {rate} | {label} | {status} | {verified} | {throughput} |")
    lines.extend(["", "### Health", "", f"- Before: {health_summary(health_before)}",
                  f"- After: {health_summary(health_after)}",
                  f"- Raw log: {raw_log or 'not recorded'}", "", "---"])
    return "\n".join(lines)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uart0-pico", required=True)
    parser.add_argument("--uart0-peer", required=True)
    parser.add_argument("--uart2", required=True)
    parser.add_argument("--uart3", required=True)
    parser.add_argument("--uart5", required=True)
    parser.add_argument("--uart1")
    parser.add_argument("--uart1-peer")
    parser.add_argument("--uart4")
    parser.add_argument("--uart4-peer")
    parser.add_argument("--uart0-baud", type=int, default=115200)
    parser.add_argument("--rates", default="115200",
                        help="Concurrent full-fixture rate; use individual tests for higher baud rates")
    parser.add_argument("--duration", type=float, default=10.0)
    parser.add_argument("--payload-bytes", type=int, default=1024)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--board", default="unknown")
    parser.add_argument("--tester", default="unknown")
    parser.add_argument("--firmware-version", default="unknown")
    parser.add_argument("--firmware-commit", default="unknown")
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--results-file", type=Path, default=DEFAULT_RESULTS_FILE)
    parser.add_argument("--no-record", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    artifact = artifact_metadata(arguments.artifact)
    arguments.artifact_path = artifact["path"]
    arguments.artifact_sha256 = artifact["sha256"]
    if (arguments.duration <= 0 or arguments.timeout <= 0 or
            arguments.payload_bytes < 32 or arguments.uart0_baud <= 0):
        print("duration, timeout, UART0 baud, and payload must be valid", file=sys.stderr)
        return 2
    for option, peer, expected in (
            ("--uart1-peer", arguments.uart1_peer, arguments.uart2),
            ("--uart4-peer", arguments.uart4_peer, arguments.uart3)):
        if not peer_path_matches(peer, expected):
            print(f"{option} must resolve to the corresponding peer endpoint", file=sys.stderr)
            return 2
    if arguments.uart1_peer and not arguments.uart1:
        print("--uart1-peer requires --uart1", file=sys.stderr)
        return 2
    if arguments.uart4_peer and not arguments.uart4:
        print("--uart4-peer requires --uart4", file=sys.stderr)
        return 2

    command = build_command(arguments)
    health_before = collect_hid_health()
    print(health_evidence(health_before), end="")
    print(f"RUN performance benchmark: {' '.join(shlex.quote(part) for part in command)}")
    completed = subprocess.run(command, cwd=REPO_ROOT, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True)
    output = completed.stdout
    print(output, end="")

    timestamp = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    health_after = collect_hid_health()
    print(health_evidence(health_after), end="")
    raw_log = None
    if not arguments.no_record:
        raw_log = write_raw_log(
            arguments.results_file.resolve(), timestamp,
            f"Command: {shlex.join(sys.argv)}\n"
            f"Artifact: {arguments.artifact_path}\nSHA-256: {arguments.artifact_sha256}\n"
            + health_evidence(health_before) + "\n"
            + output + "\n" + health_evidence(health_after))
    entry = format_result_entry(arguments, timestamp, completed.returncode, output,
                                health_before, health_after, raw_log)
    if not arguments.no_record:
        prepend_result(arguments.results_file.resolve(), entry)
        print(f"Recorded result in {arguments.results_file}")
    return completed.returncode if health_is_clean(health_after, health_before) else 1


if __name__ == "__main__":
    raise SystemExit(main())

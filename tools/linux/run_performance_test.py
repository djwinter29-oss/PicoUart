#!/usr/bin/env python3
"""Run the PicoUart concurrent performance benchmark and record its result."""

from __future__ import annotations

import argparse
import datetime as dt
import re
import shlex
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

from hardware_test_result import prepend_result

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_RESULTS_FILE = REPO_ROOT / "docs/tests/performance-test-results.md"
PASS_PATTERN = re.compile(
    r"^PASS (?P<label>[^:]+): (?P<bytes>[0-9]+) bytes, (?P<throughput>[0-9.]+) B/s$",
    re.MULTILINE,
)
FAIL_PATTERN = re.compile(r"^FAIL (?P<label>[^:]+): (?P<error>.+)$", re.MULTILINE)


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
    if arguments.uart4:
        command.extend(["--uart4", arguments.uart4])
    return command


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


def format_result_entry(arguments: SimpleNamespace,
                        timestamp: str,
                        result: int,
                        output: str) -> str:
    parsed = parse_benchmark_output(output)
    overall = "PASS" if result == 0 else "FAIL"
    expected_labels = [
        "uart0-pico-to-peer", "uart0-peer-to-pico", "uart2-to-uart3",
        "uart3-to-uart2", "uart5-loopback",
    ]
    if arguments.uart1:
        expected_labels.append("uart1-loopback")
    if arguments.uart4:
        expected_labels.append("uart4-loopback")

    lines = [
        f"## {timestamp} - {arguments.board} - Performance Test",
        "",
        f"**Result:** `{overall}`  ",
        f"**Tester:** {arguments.tester}  ",
        f"**Firmware version:** {arguments.firmware_version}  ",
        f"**Firmware commit:** `{arguments.firmware_commit}`  ",
        f"**Board:** `{arguments.board}`  ",
        f"**Test date/time:** `{timestamp}`  ",
        "**Wiring:** Performance benchmark fixture  ",
        "**RTS/CTS:** `disabled`  ",
        "",
        "#### Test Configuration",
        "",
        "| Setting | Value |",
        "| --- | --- |",
        f"| Baud rate(s) | {arguments.rates} |",
        f"| UART0 baud | {arguments.uart0_baud} |",
        f"| Duration per rate | {arguments.duration} seconds |",
        f"| Payload size | {arguments.payload_bytes} bytes |",
        f"| Timeout | {arguments.timeout} seconds |",
        f"| Command | `{' '.join(shlex.quote(part) for part in build_command(arguments))}` |",
        "",
        "#### Results",
        "",
        "| Link | Result | Verified bytes | Throughput / error |",
        "| --- | --- | ---: | --- |",
    ]
    for label in expected_labels:
        status, verified, throughput = parsed.get(label, ("NOT REPORTED", "-", "-"))
        lines.append(f"| {label} | {status} | {verified} | {throughput} |")
    lines.extend(["", "#### Command Output", "", "```text", output.rstrip(), "```", "", "---"])
    return "\n".join(lines)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uart0-pico", required=True)
    parser.add_argument("--uart0-peer", required=True)
    parser.add_argument("--uart2", required=True)
    parser.add_argument("--uart3", required=True)
    parser.add_argument("--uart5", required=True)
    parser.add_argument("--uart1")
    parser.add_argument("--uart4")
    parser.add_argument("--uart0-baud", type=int, default=115200)
    parser.add_argument("--rates", default="115200,460800,921600,1000000")
    parser.add_argument("--duration", type=float, default=10.0)
    parser.add_argument("--payload-bytes", type=int, default=1024)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--board", default="unknown")
    parser.add_argument("--tester", default="unknown")
    parser.add_argument("--firmware-version", default="unknown")
    parser.add_argument("--firmware-commit", default="unknown")
    parser.add_argument("--results-file", type=Path, default=DEFAULT_RESULTS_FILE)
    parser.add_argument("--no-record", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if (arguments.duration <= 0 or arguments.timeout <= 0 or
            arguments.payload_bytes < 32 or arguments.uart0_baud <= 0):
        print("duration, timeout, UART0 baud, and payload must be valid", file=sys.stderr)
        return 2

    command = build_command(arguments)
    print(f"RUN performance benchmark: {' '.join(shlex.quote(part) for part in command)}")
    completed = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True)
    output = completed.stdout + completed.stderr
    print(output, end="")

    timestamp = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    entry = format_result_entry(arguments, timestamp, completed.returncode, output)
    if not arguments.no_record:
        prepend_result(arguments.results_file.resolve(), entry)
        print(f"Recorded result in {arguments.results_file}")
    return completed.returncode


if __name__ == "__main__":
    raise SystemExit(main())

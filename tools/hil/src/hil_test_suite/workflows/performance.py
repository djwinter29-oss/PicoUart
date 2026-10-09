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

from ..support.results import artifact_metadata, prepend_result
from ..support.health import collect_hid_health, health_evidence, health_is_clean, health_summary
from ..support.paths import REPO_ROOT
from ..support.repository import git_metadata

DEFAULT_RESULTS_FILE = REPO_ROOT / "build/hil-results.md"
DEFAULT_RATES = "115200,128000,153600,230400,256000,460800,921600,1000000,2000000,3000000"
PASS_PATTERN = re.compile(
    r"^PASS (?P<label>[^:]+): (?P<bytes>[0-9]+) bytes, (?P<throughput>[0-9.]+) B/s$",
    re.MULTILINE,
)
FAIL_PATTERN = re.compile(r"^FAIL (?P<label>[^:]+): (?P<error>.+)$", re.MULTILINE)
RATE_PATTERN = re.compile(r"^(?:Benchmarking .*? at |Incremental rate: )(?P<rate>[0-9]+) baud", re.MULTILINE)


def build_command(arguments: SimpleNamespace) -> list[str]:
    """Build the internal serial_stress_benchmark invocation."""
    command = [
        sys.executable,
        "-u",
        "-m",
        "hil_test_suite.serial.stress",
        "--cdc0",
        arguments.cdc0,
        "--cdc1",
        arguments.cdc1,
        "--cdc2",
        arguments.cdc2,
        "--cdc3",
        arguments.cdc3,
        "--cdc4",
        arguments.cdc4,
        "--cdc5",
        arguments.cdc5,
        "--duration",
        str(arguments.duration),
        "--payload-bytes",
        str(arguments.payload_bytes),
        "--timeout",
        str(arguments.timeout),
        "--check-hid-health",
    ]
    if getattr(arguments, "incremental", False):
        command.extend(
            [
                "--incremental",
                "--incremental-start-rate",
                str(arguments.incremental_start_rate),
                "--incremental-rate-step",
                str(arguments.incremental_rate_step),
                "--incremental-max-rate",
                str(arguments.incremental_max_rate),
            ]
        )
    else:
        command.extend(["--rates", arguments.rates])
    return command


def parse_benchmark_output(output: str) -> dict[str, tuple[str, str, str]]:
    """Return label -> (result, verified bytes, throughput) from benchmark output."""
    results = {
        match.group("label"): ("PASS", match.group("bytes"), match.group("throughput"))
        for match in PASS_PATTERN.finditer(output)
    }
    results.update(
        {match.group("label"): ("FAIL", "-", match.group("error")) for match in FAIL_PATTERN.finditer(output)}
    )
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
                "PASS",
                pass_match.group("bytes"),
                pass_match.group("throughput"),
            )
        elif fail_match:
            results[(current_rate, fail_match.group("label"))] = ("FAIL", "-", fail_match.group("error"))
    return results


def format_result_entry(
    arguments: SimpleNamespace,
    timestamp: str,
    result: int,
    output: str,
    health_before: dict | None = None,
    health_after: dict | None = None,
) -> str:
    parsed = parse_benchmark_output_by_rate(output)
    attempted_rates = {int(rate) for rate in RATE_PATTERN.findall(output)}
    clean = result == 0 and health_is_clean(health_after, health_before)
    overall = "PASS" if clean else "FAIL"
    expected_labels = [
        "cdc0-to-cdc2",
        "cdc2-to-cdc0",
        "cdc3-to-cdc4",
        "cdc4-to-cdc3",
        "cdc1-loopback",
        "cdc5-loopback",
    ]

    lines = [
        f"## {timestamp} - {arguments.board} - Performance Test",
        "",
        f"**Result:** `{overall}`",
        f"**Firmware:** {arguments.firmware_version}, `{arguments.firmware_commit}`",
        f"**Runner Git commit:** `{getattr(arguments, 'runner_git_commit', 'unknown')}`",
        f"**Runner worktree:** `{getattr(arguments, 'runner_worktree', 'unknown')}`",
        f"**Board:** `{arguments.board}`",
        f"**Test date/time:** `{timestamp}`",
        "**Wiring:** Fixed six-channel HIL fixture",
        "**RTS/CTS:** disabled",
        "",
        "### Configuration",
        "",
        f"- Baud rates: {arguments.rates}",
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
    if getattr(arguments, "incremental", False):
        rates = list(dict.fromkeys(int(rate) for rate in RATE_PATTERN.findall(output)))
    else:
        rates = [int(item) for item in arguments.rates.split(",")]
    for rate in rates:
        for label in expected_labels:
            missing_status = "NOT REPORTED" if rate in attempted_rates else "NOT RUN"
            status, verified, throughput = parsed.get((rate, label), (missing_status, "-", "-"))
            lines.append(f"| {rate} | {label} | {status} | {verified} | {throughput} |")
    if getattr(arguments, "incremental", False):
        passing_rates = [
            rate
            for rate in rates
            if all(parsed.get((rate, label), ("NOT REPORTED",))[0] == "PASS" for label in expected_labels)
        ]
        failing_rates = [
            rate
            for rate in rates
            if any(parsed.get((rate, label), ("NOT REPORTED",))[0] != "PASS" for label in expected_labels)
        ]
        lines.extend(
            [
                "",
                f"**Highest supported concurrent rate:** `{max(passing_rates) if passing_rates else 'none'}`",
                f"**First failed tested rate:** `{failing_rates[0] if failing_rates else 'not reached'}`",
            ]
        )
    lines.extend(
        [
            "",
            "### Per-Rate HID Health",
            "",
            *[f"- {line}" for line in output.splitlines() if line.startswith("HID rate ")],
            "",
            "### Health",
            "",
            f"- Before: {health_summary(health_before)}",
            f"- After: {health_summary(health_after)}",
            "",
            "---",
        ]
    )
    return "\n".join(lines)


def build_parser(add_help: bool = True) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, add_help=add_help)
    for channel in range(6):
        parser.add_argument(f"--cdc{channel}", required=True)
    parser.add_argument(
        "--rates", default=DEFAULT_RATES, help="Concurrent full-fixture rates, comma-separated (default: %(default)s)"
    )
    parser.add_argument("--incremental", action="store_true", help="Increase baud until the first failing rate")
    parser.add_argument("--incremental-start-rate", type=int, default=460800)
    parser.add_argument("--incremental-rate-step", type=int, default=100000)
    parser.add_argument("--incremental-max-rate", type=int, default=3000000)
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
    return parser


def parse_arguments(argv: list[str] | None = None) -> argparse.Namespace:
    return build_parser().parse_args(argv)


def main(arguments: argparse.Namespace | None = None) -> int:
    if arguments is None:
        arguments = parse_arguments()
    arguments.runner_git_commit, arguments.runner_worktree = git_metadata()
    artifact = artifact_metadata(arguments.artifact)
    arguments.artifact_path = artifact["path"]
    arguments.artifact_sha256 = artifact["sha256"]
    if arguments.duration <= 0 or arguments.timeout <= 0 or arguments.payload_bytes < 32:
        print("duration, timeout, and payload must be valid", file=sys.stderr)
        return 2

    command = build_command(arguments)
    health_before = collect_hid_health()
    print(health_evidence(health_before), end="")
    print(f"RUN performance benchmark: {' '.join(shlex.quote(part) for part in command)}")
    completed = subprocess.run(command, cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    output = completed.stdout
    print(output, end="")

    timestamp = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    health_after = collect_hid_health()
    print(health_evidence(health_after), end="")
    entry = format_result_entry(arguments, timestamp, completed.returncode, output, health_before, health_after)
    if not arguments.no_record:
        prepend_result(arguments.results_file.resolve(), entry)
        print(f"Recorded result in {arguments.results_file}")
    return completed.returncode if health_is_clean(health_after, health_before) else 1


if __name__ == "__main__":
    raise SystemExit(main())

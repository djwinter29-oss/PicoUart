#!/usr/bin/env python3
"""Run the complete PicoUart functional and performance hardware test."""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

from ..support.results import artifact_metadata
from ..support.paths import REPO_ROOT
from ..support.repository import git_metadata
from .performance import parse_benchmark_output_by_rate

DEFAULT_RECORDS_DIR = REPO_ROOT / "docs/tests/records"
FUNCTIONAL_CASES = (
    ("HW UART0 to PIO UART2", "CDC0 <-> CDC2", ("pico-to-peer", "peer-to-pico")),
    ("PIO UART3 to PIO UART4", "CDC3 <-> CDC4", ("pico-to-peer", "peer-to-pico")),
    ("HW UART1 loopback", "CDC1 loopback", ("pico-loopback",)),
    ("PIO UART5 loopback", "CDC5 loopback", ("pico-loopback",)),
)
PERFORMANCE_LINKS = (
    "cdc0-to-cdc2",
    "cdc2-to-cdc0",
    "cdc3-to-cdc4",
    "cdc4-to-cdc3",
    "cdc1-loopback",
    "cdc5-loopback",
)
FUNCTIONAL_RESULT_PATTERN = re.compile(r"^(PASS|FAIL) (pico-to-peer|peer-to-pico|pico-loopback): (.+)$")
HEALTH_SUMMARY_PATTERN = re.compile(r"^HID health summary: (.+)$", re.MULTILINE)


def functional_summary_rows(result: tuple[int, str] | None) -> list[tuple[str, str, str, str]]:
    """Summarize functional link outcomes without embedding child transcripts."""
    observed: dict[str, list[tuple[str, str, int | None]]] = {label: [] for label, _, _ in FUNCTIONAL_CASES}
    output = result[1] if result is not None else ""
    active_case = None
    for line in output.splitlines():
        if line.startswith("RUN "):
            active_case = next((label for label, _, _ in FUNCTIONAL_CASES if line.startswith(f"RUN {label}:")), None)
            continue
        match = FUNCTIONAL_RESULT_PATTERN.match(line)
        if active_case is not None and match:
            count_match = re.search(r"([0-9]+) bytes", match.group(3))
            count = int(count_match.group(1)) if count_match else None
            observed[active_case].append((match.group(2), match.group(1), count))

    rows = []
    for label, connection, expected_directions in FUNCTIONAL_CASES:
        outcomes = observed[label]
        if result is None or f"RUN {label}:" not in output:
            status = "NOT RUN"
        elif any(outcome == "FAIL" for _, outcome, _ in outcomes):
            status = "FAIL"
        elif len(outcomes) == len(expected_directions) and {direction for direction, _, _ in outcomes} == set(
            expected_directions
        ):
            status = "PASS"
        else:
            status = "FAIL" if result[0] != 0 else "NOT REPORTED"
        verified_bytes = sum(count for _, outcome, count in outcomes if outcome == "PASS" and count is not None)
        rows.append((label, connection, status, str(verified_bytes) if verified_bytes else "-"))
    return rows


def performance_summary_rows(
    rates: str, result: tuple[int, str] | None
) -> list[tuple[str, str, str, str, str]]:
    """Summarize each configured rate and stream from benchmark output."""
    parsed = parse_benchmark_output_by_rate(result[1]) if result is not None else {}
    rows = []
    for rate in (value.strip() for value in rates.split(",") if value.strip()):
        for label in PERFORMANCE_LINKS:
            outcome = parsed.get((int(rate), label))
            if result is None:
                status, verified, metric = "NOT RUN", "-", "-"
            elif outcome is None:
                status, verified, metric = "NOT REPORTED", "-", "-"
            else:
                status, verified, metric = outcome
                if status == "PASS":
                    metric = f"{metric} B/s"
            rows.append((rate, label, status, verified, metric))
    return rows


def final_health_summary(*results: tuple[int, str] | None) -> str:
    for result in reversed(results):
        if result is not None:
            summaries = HEALTH_SUMMARY_PATTERN.findall(result[1])
            if summaries:
                return summaries[-1]
    return "not reported"


def build_functional_command(arguments: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        "-m",
        "hil_test_suite.workflows.functional",
        "--pico-cdc0",
        arguments.pico_cdc0,
        "--pico-cdc1",
        arguments.pico_cdc1,
        "--pico-cdc2",
        arguments.pico_cdc2,
        "--pico-cdc3",
        arguments.pico_cdc3,
        "--pico-cdc4",
        arguments.pico_cdc4,
        "--pico-cdc5",
        arguments.pico_cdc5,
        "--baud",
        str(arguments.functional_baud),
        "--payload-bytes",
        str(arguments.functional_payload_bytes),
        "--timeout",
        str(arguments.timeout),
        "--board",
        arguments.board,
        "--tester",
        arguments.tester,
        "--firmware-version",
        arguments.firmware_version,
        "--firmware-commit",
        arguments.firmware_commit,
        "--no-record",
    ]
    if getattr(arguments, "artifact", None):
        command.extend(["--artifact", str(arguments.artifact)])
    return command


def build_performance_command(arguments: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        "-m",
        "hil_test_suite.workflows.performance",
        "--cdc0",
        arguments.pico_cdc0,
        "--cdc1",
        arguments.pico_cdc1,
        "--cdc2",
        arguments.pico_cdc2,
        "--cdc3",
        arguments.pico_cdc3,
        "--cdc4",
        arguments.pico_cdc4,
        "--cdc5",
        arguments.pico_cdc5,
        "--rates",
        arguments.rates,
        "--duration",
        str(arguments.duration),
        "--payload-bytes",
        str(arguments.performance_payload_bytes),
        "--timeout",
        str(arguments.timeout),
        "--board",
        arguments.board,
        "--tester",
        arguments.tester,
        "--firmware-version",
        arguments.firmware_version,
        "--firmware-commit",
        arguments.firmware_commit,
        "--no-record",
    ]
    if getattr(arguments, "artifact", None):
        command.extend(["--artifact", str(arguments.artifact)])
    return command


def run_child(label: str, command: list[str]) -> tuple[int, str]:
    command_text = shlex.join(command)
    print(f"RUN {label}: {command_text}")
    process = subprocess.Popen(command, cwd=REPO_ROOT, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    chunks: list[bytes] = []
    assert process.stdout is not None
    while True:
        chunk = os.read(process.stdout.fileno(), 4096)
        if not chunk:
            break
        chunks.append(chunk)
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
    returncode = process.wait()
    output = b"".join(chunks).decode(errors="replace")
    return returncode, f"Command: {command_text}\n{output}"


def format_result_entry(
    arguments: argparse.Namespace,
    timestamp: str,
    functional: tuple[int, str] | None,
    performance: tuple[int, str] | None,
) -> str:
    functional_code = functional[0] if functional else None
    performance_code = performance[0] if performance else None
    if any(code not in (None, 0) for code in (functional_code, performance_code)):
        overall = "FAIL"
    elif functional_code is None or performance_code is None or not getattr(arguments, "full_fixture", False):
        overall = "PARTIAL"
    elif functional_code == 0 and performance_code == 0:
        overall = "PASS"
    else:
        overall = "FAIL"

    functional_status = "PASS" if functional_code == 0 else "FAIL" if functional_code is not None else "NOT RUN"
    performance_status = "PASS" if performance_code == 0 else "FAIL" if performance_code is not None else "NOT RUN"
    lines = [
        "# PicoUart HIL Record",
        "",
        f"- **Overall result:** `{overall}`",
        f"- **Run time (UTC):** `{timestamp}`",
        f"- **Board:** `{arguments.board}`",
        f"- **Tester:** {arguments.tester}",
        f"- **Firmware:** `{arguments.firmware_version}`",
        f"- **Firmware commit:** `{arguments.firmware_commit}`",
        f"- **Runner Git commit:** `{getattr(arguments, 'runner_git_commit', 'unknown')}`",
        f"- **Runner worktree:** `{getattr(arguments, 'runner_worktree', 'unknown')}`",
        f"- **Artifact:** {getattr(arguments, 'artifact_path', 'not supplied')}",
        f"- **Artifact SHA-256:** `{getattr(arguments, 'artifact_sha256', 'not supplied')}`",
        "- **Fixture:** HW UART0 <-> PIO UART2, PIO UART3 <-> PIO UART4, HW UART1 loopback, PIO UART5 loopback",
        "- **RTS/CTS:** disabled",
        "",
        "## Functional Summary",
        "",
        f"**Phase result:** `{functional_status}`",
        "",
        "| Stage | Connection | Result | Verified bytes |",
        "| --- | --- | --- | ---: |",
    ]
    lines.extend(
        f"| {label} | {connection} | {status} | {count} |"
        for label, connection, status, count in functional_summary_rows(functional)
    )
    lines.extend(
        [
            "",
            "## Concurrent Performance Summary",
            "",
            f"**Phase result:** `{performance_status}`",
            "",
            "| Rate (baud) | Stream | Result | Verified bytes | Throughput / error |",
            "| ---: | --- | --- | ---: | --- |",
        ]
    )
    lines.extend(
        f"| {rate} | {label} | {status} | {verified} | {metric} |"
        for rate, label, status, verified, metric in performance_summary_rows(
            getattr(arguments, "rates", ""), performance
        )
    )
    lines.extend(
        [
            "",
            "## HID Health",
            "",
            final_health_summary(functional, performance),
            "",
        ]
    )
    return "\n".join(lines)


def write_hil_record(record_dir: Path, run_at: dt.datetime, board: str, content: str) -> Path:
    """Write one uniquely named UTC HIL record without overwriting earlier runs."""
    record_dir.mkdir(parents=True, exist_ok=True)
    board_token = re.sub(r"[^a-z0-9]+", "-", board.lower()).strip("-") or "unknown"
    stem = f"{run_at.strftime('%Y-%m-%d-%H%M%SZ')}-{board_token}-hil"
    suffix = 1
    while True:
        numbered_stem = stem if suffix == 1 else f"{stem}-{suffix:02d}"
        path = record_dir / f"{numbered_stem}.md"
        try:
            with path.open("x", encoding="utf-8") as record_file:
                record_file.write(content.rstrip() + "\n")
            return path
        except FileExistsError:
            suffix += 1


def build_parser(add_help: bool = True) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, add_help=add_help)
    parser.add_argument("--pico-cdc0", required=True)
    parser.add_argument("--pico-cdc1", required=True)
    parser.add_argument("--pico-cdc2", required=True)
    parser.add_argument("--pico-cdc3", required=True)
    parser.add_argument("--pico-cdc4", required=True)
    parser.add_argument("--pico-cdc5", required=True)
    parser.add_argument("--functional-baud", type=int, default=115200)
    parser.add_argument("--functional-payload-bytes", type=int, default=64)
    parser.add_argument(
        "--rates", default="115200", help="Concurrent full-fixture rate; use individual tests for higher baud rates"
    )
    parser.add_argument("--duration", type=float, default=10.0)
    parser.add_argument("--performance-payload-bytes", type=int, default=1024)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--board", default="unknown")
    parser.add_argument("--tester", default="unknown")
    parser.add_argument("--firmware-version", default="unknown")
    parser.add_argument("--firmware-commit", default="unknown")
    parser.add_argument("--artifact", type=Path, help="flashed ELF/UF2 artifact to hash into the evidence")
    parser.add_argument("--record-dir", type=Path, default=DEFAULT_RECORDS_DIR)
    parser.add_argument("--skip-functional", action="store_true")
    parser.add_argument("--skip-performance", action="store_true")
    parser.add_argument("--continue-after-functional-failure", action="store_true")
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
    if arguments.skip_functional and arguments.skip_performance:
        print("at least one test phase must run", file=sys.stderr)
        return 2
    arguments.full_fixture = True

    functional = None
    performance = None
    if not arguments.skip_functional:
        functional = run_child("functional test", build_functional_command(arguments))

    if not arguments.skip_performance and (
        functional is None or functional[0] == 0 or arguments.continue_after_functional_failure
    ):
        performance = run_child("performance test", build_performance_command(arguments))
    elif not arguments.skip_performance:
        print("SKIP performance test: functional test failed", file=sys.stderr)

    run_at = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    timestamp = run_at.isoformat()
    entry = format_result_entry(arguments, timestamp, functional, performance)
    if not arguments.no_record:
        record_path = write_hil_record(arguments.record_dir.resolve(), run_at, arguments.board, entry)
        print(f"Recorded HIL run in {record_path}")

    codes = [code for result in (functional, performance) if result is not None for code in [result[0]]]
    return 0 if codes and all(code == 0 for code in codes) else 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Run the staged PicoUart functional test and record its result."""

from __future__ import annotations

import argparse
import datetime as dt
import os
import shlex
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

from hardware_test_result import artifact_metadata, prepend_result, write_raw_log
from hardware_test_health import (collect_hid_health, health_evidence, health_is_clean,
                                  health_summary)

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
DEFAULT_RESULTS_FILE = REPO_ROOT / "docs/tests/performance-test-results.md"


def build_stage_commands(arguments: SimpleNamespace) -> list[tuple[str, list[str]]]:
    """Build the four documented serial_bridge_test invocations."""
    bridge = str(SCRIPT_DIR / "serial_bridge_test.py")
    common = [sys.executable, bridge, "--payload-bytes", str(arguments.payload_bytes),
              "--timeout", str(arguments.timeout), "--baud", str(arguments.baud)]
    stages = [
        ("Debug Probe to HW UART0", common + [
            "--pico-port", arguments.pico_cdc0, "--peer-port", arguments.debug_probe,
            "--label", "stage1-debug-probe-hw-uart",
        ]),
        ("HW UART1 to PIO UART2", common + [
            "--pico-port", arguments.pico_cdc1, "--peer-port", arguments.pico_cdc2,
            "--label", "stage2-hw-to-pio",
        ]),
        ("PIO UART3 to PIO UART4", common + [
            "--pico-port", arguments.pico_cdc3, "--peer-port", arguments.pico_cdc4,
            "--label", "stage3-pio-to-pio",
        ]),
        ("PIO UART5 loopback", common + [
            "--pico-port", arguments.pico_cdc5, "--loopback",
            "--label", "stage4-pio-loopback",
        ]),
    ]
    if arguments.stage == "all":
        return stages
    return [stages[int(arguments.stage) - 1]]


def format_result_entry(arguments: SimpleNamespace,
                        timestamp: str,
                        stages: list[tuple[str, int, str]],
                        health_before: dict | None = None,
                        health_after: dict | None = None,
                        raw_log: Path | None = None) -> str:
    clean = stages and all(code == 0 for _, code, _ in stages) and health_is_clean(
        health_after, health_before)
    overall = "PASS" if clean and len(stages) == 4 else "PARTIAL" if clean else "FAIL"
    stage_results = {label: code == 0 for label, code, _ in stages}
    lines = [
        f"## {timestamp} - {arguments.board} - Functional Test",
        "",
        f"**Result:** `{overall}`",
        f"**Firmware:** {arguments.firmware_version}, `{arguments.firmware_commit}`",
        f"**Board:** `{arguments.board}`",
        f"**Test date/time:** `{timestamp}`",
        "**Wiring:** Self-test stages 1-4",
        "**RTS/CTS:** disabled",
        "",
        "### Configuration",
        "",
        f"- Baud rate: {arguments.baud}",
        f"- Payload: {arguments.payload_bytes} bytes",
        f"- Artifact: {getattr(arguments, 'artifact_path', 'not supplied')}",
        f"- Artifact SHA-256: `{getattr(arguments, 'artifact_sha256', 'not supplied')}`",
        f"- HID firmware version: `{health_after.get('firmware_version', 'unknown') if health_after else 'unknown'}`",
        "",
        "### Results",
        "",
        "| Link | Result |",
        "| --- | --- |",
    ]
    for label in (
        "Debug Probe to HW UART0",
        "HW UART1 to PIO UART2",
        "PIO UART3 to PIO UART4",
        "PIO UART5 loopback",
    ):
        if label in stage_results:
            lines.append(f"| {label} | {'PASS' if stage_results[label] else 'FAIL'} |")
    lines.extend(["", "### Health", "", f"- Before: {health_summary(health_before)}",
                  f"- After: {health_summary(health_after)}",
                  f"- Raw log: {raw_log or 'not recorded'}", "", "---"])
    return "\n".join(lines)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pico-cdc0", required=True)
    parser.add_argument("--debug-probe", required=True)
    parser.add_argument("--pico-cdc1", required=True)
    parser.add_argument("--pico-cdc2", required=True)
    parser.add_argument("--pico-cdc3", required=True)
    parser.add_argument("--pico-cdc4", required=True)
    parser.add_argument("--pico-cdc5", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--payload-bytes", type=int, default=64)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--board", default="unknown")
    parser.add_argument("--tester", default="unknown")
    parser.add_argument("--firmware-version", default="unknown")
    parser.add_argument("--firmware-commit", default="unknown")
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--results-file", type=Path, default=DEFAULT_RESULTS_FILE)
    parser.add_argument("--no-record", action="store_true")
    parser.add_argument("--continue-on-failure", action="store_true")
    parser.add_argument("--stage", choices=("all", "1", "2", "3", "4"), default="all",
                        help="Run one connection, or all prewired connections")
    return parser.parse_args()


def fixture_paths_valid(arguments: argparse.Namespace) -> bool:
    """Reject endpoint aliases that would invalidate the claimed topology."""
    paths = [
        ("--pico-cdc0", arguments.pico_cdc0),
        ("--debug-probe", arguments.debug_probe),
        ("--pico-cdc1", arguments.pico_cdc1),
        ("--pico-cdc2", arguments.pico_cdc2),
        ("--pico-cdc3", arguments.pico_cdc3),
        ("--pico-cdc4", arguments.pico_cdc4),
        ("--pico-cdc5", arguments.pico_cdc5),
    ]
    seen: dict[str, str] = {}
    for name, path in paths:
        resolved = os.path.realpath(path)
        if resolved in seen:
            print(f"{name} resolves to the same endpoint as {seen[resolved]}", file=sys.stderr)
            return False
        seen[resolved] = name
    return True


def main() -> int:
    arguments = parse_arguments()
    artifact = artifact_metadata(arguments.artifact)
    arguments.artifact_path = artifact["path"]
    arguments.artifact_sha256 = artifact["sha256"]
    if arguments.payload_bytes < 1 or arguments.timeout <= 0 or arguments.baud <= 0:
        print("baud, payload, and timeout must be greater than zero", file=sys.stderr)
        return 2
    if not fixture_paths_valid(arguments):
        return 2
    stages: list[tuple[str, int, str]] = []
    health_before = collect_hid_health()
    print(health_evidence(health_before), end="")
    for label, command in build_stage_commands(arguments):
        print(f"RUN {label}: {' '.join(shlex.quote(part) for part in command)}")
        completed = subprocess.run(command, cwd=REPO_ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True)
        output = completed.stdout
        print(output, end="")
        stages.append((label, completed.returncode, output))
        if completed.returncode != 0 and not arguments.continue_on_failure:
            break

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
            + "\n".join(f"[{label}] exit={code}\n{output}" for label, code, output in stages)
            + "\n" + health_evidence(health_after))
    entry = format_result_entry(arguments, timestamp, stages, health_before, health_after, raw_log)
    if not arguments.no_record:
        prepend_result(arguments.results_file.resolve(), entry)
        print(f"Recorded result in {arguments.results_file}")

    return 0 if stages and all(code == 0 for _, code, _ in stages) and \
        health_is_clean(health_after, health_before) else 1


if __name__ == "__main__":
    raise SystemExit(main())

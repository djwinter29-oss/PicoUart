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

from hardware_test_result import artifact_metadata

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_RECORDS_DIR = REPO_ROOT / "docs/tests/records"


def build_functional_command(arguments: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        str(SCRIPT_DIR / "run_functional_test.py"),
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
        str(SCRIPT_DIR / "run_performance_test.py"),
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

    def transcript_block(result: tuple[int, str] | None) -> str:
        if result is None:
            return "Not run."
        output = result[1].rstrip() or "No output."
        longest_fence = max((len(match) for match in re.findall(r"`+", output)), default=0)
        fence = "`" * max(3, longest_fence + 1)
        return f"{fence}text\n{output}\n{fence}"

    return "\n".join(
        [
            "# PicoUart HIL Record",
            "",
            f"- **Result:** `{overall}`",
            f"- **Run time (UTC):** `{timestamp}`",
            f"- **Board:** `{arguments.board}`",
            f"- **Tester:** {arguments.tester}",
            f"- **Firmware:** `{arguments.firmware_version}`",
            f"- **Source commit:** `{arguments.firmware_commit}`",
            f"- **Artifact:** {getattr(arguments, 'artifact_path', 'not supplied')}",
            f"- **Artifact SHA-256:** `{getattr(arguments, 'artifact_sha256', 'not supplied')}`",
            "- **Fixture:** HW UART0↔PIO UART2, PIO UART3↔PIO UART4, HW UART1 loopback, PIO UART5 loopback",
            "- **RTS/CTS:** disabled",
            "",
            "## Functional Test",
            "",
            f"**Result:** `{'PASS' if functional_code == 0 else 'FAIL' if functional_code is not None else 'NOT RUN'}`",
            "",
            transcript_block(functional),
            "",
            "## Performance Test",
            "",
            f"**Result:** `{'PASS' if performance_code == 0 else 'FAIL' if performance_code is not None else 'NOT RUN'}`",
            "",
            transcript_block(performance),
            "",
            "## Review Notes",
            "",
            "Confirm per-link verified bytes, HID health, and any anomalies before treating this record as release evidence.",
            "",
        ]
    )


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


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pico-cdc0", required=True)
    parser.add_argument("--pico-cdc1", required=True)
    parser.add_argument("--pico-cdc2", required=True)
    parser.add_argument("--pico-cdc3", required=True)
    parser.add_argument("--pico-cdc4", required=True)
    parser.add_argument("--pico-cdc5", required=True)
    parser.add_argument("--functional-baud", type=int, default=115200)
    parser.add_argument("--functional-payload-bytes", type=int, default=64)
    parser.add_argument("--uart0-baud", type=int, default=115200)
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
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
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

#!/usr/bin/env python3
"""Run the complete PicoUart functional and performance hardware test."""

from __future__ import annotations

import argparse
import datetime as dt
import shlex
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

from hardware_test_result import prepend_result, write_raw_log

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_RESULTS_FILE = REPO_ROOT / "docs/tests/performance-test-results.md"


def build_functional_command(arguments: argparse.Namespace) -> list[str]:
    return [
        sys.executable,
        str(SCRIPT_DIR / "run_functional_test.py"),
        "--pico-cdc0", arguments.pico_cdc0,
        "--debug-probe", arguments.debug_probe,
        "--pico-cdc1", arguments.pico_cdc1,
        "--pico-cdc2", arguments.pico_cdc2,
        "--pico-cdc3", arguments.pico_cdc3,
        "--pico-cdc4", arguments.pico_cdc4,
        "--pico-cdc5", arguments.pico_cdc5,
        "--baud", str(arguments.functional_baud),
        "--payload-bytes", str(arguments.functional_payload_bytes),
        "--timeout", str(arguments.timeout),
        "--board", arguments.board,
        "--tester", arguments.tester,
        "--firmware-version", arguments.firmware_version,
        "--firmware-commit", arguments.firmware_commit,
        "--no-record",
    ]


def build_performance_command(arguments: argparse.Namespace) -> list[str]:
    command = [
        sys.executable,
        str(SCRIPT_DIR / "run_performance_test.py"),
        "--uart0-pico", arguments.pico_cdc0,
        "--uart0-peer", arguments.debug_probe,
        "--uart2", arguments.pico_cdc2,
        "--uart3", arguments.pico_cdc3,
        "--uart5", arguments.pico_cdc5,
        "--uart0-baud", str(arguments.uart0_baud),
        "--rates", arguments.rates,
        "--duration", str(arguments.duration),
        "--payload-bytes", str(arguments.performance_payload_bytes),
        "--timeout", str(arguments.timeout),
        "--board", arguments.board,
        "--tester", arguments.tester,
        "--firmware-version", arguments.firmware_version,
        "--firmware-commit", arguments.firmware_commit,
        "--no-record",
    ]
    if arguments.pico_cdc1:
        command.extend(["--uart1", arguments.pico_cdc1])
    if arguments.pico_cdc4:
        command.extend(["--uart4", arguments.pico_cdc4])
    return command


def run_child(label: str, command: list[str]) -> tuple[int, str]:
    print(f"RUN {label}: {' '.join(shlex.quote(part) for part in command)}")
    completed = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True)
    output = completed.stdout + completed.stderr
    print(output, end="")
    return completed.returncode, output


def format_result_entry(arguments: argparse.Namespace,
                        timestamp: str,
                        functional: tuple[int, str] | None,
                        performance: tuple[int, str] | None,
                        raw_log: Path | None = None) -> str:
    functional_code = functional[0] if functional else None
    performance_code = performance[0] if performance else None
    if any(code not in (None, 0) for code in (functional_code, performance_code)):
        overall = "FAIL"
    elif functional_code == 0 and performance_code == 0:
        overall = "PASS"
    elif functional_code is None or performance_code is None:
        overall = "PARTIAL"
    else:
        overall = "FAIL"

    lines = [
        f"## {timestamp} - {arguments.board} - Hardware Test",
        "",
        f"**Result:** `{overall}`",
        f"**Firmware:** {arguments.firmware_version}, `{arguments.firmware_commit}`",
        f"**Board:** `{arguments.board}`",
        f"**Test date/time:** `{timestamp}`",
        "**Wiring:** Self-test stages 1-4 and performance fixture",
        "**RTS/CTS:** disabled",
        "",
        "### Results",
        "",
        f"- Functional test: {'PASS' if functional_code == 0 else 'FAIL' if functional_code is not None else 'NOT RUN'}",
        f"- Performance test: {'PASS' if performance_code == 0 else 'FAIL' if performance_code is not None else 'NOT RUN'}",
        "",
        "### Health",
        "",
        "- RX overflows: check with `pico_uart_hid.py overruns`",
        "- HID errors: check with `pico_uart_hid.py monitor`",
        f"- Raw log: {raw_log or 'not recorded'}",
        "",
        "---",
    ]
    return "\n".join(lines)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pico-cdc0", required=True)
    parser.add_argument("--debug-probe", required=True)
    parser.add_argument("--pico-cdc1", help="Optional CDC1 endpoint for performance")
    parser.add_argument("--pico-cdc2", required=True)
    parser.add_argument("--pico-cdc3", required=True)
    parser.add_argument("--pico-cdc4", help="Optional CDC4 endpoint for performance")
    parser.add_argument("--pico-cdc5", required=True)
    parser.add_argument("--functional-baud", type=int, default=115200)
    parser.add_argument("--functional-payload-bytes", type=int, default=64)
    parser.add_argument("--uart0-baud", type=int, default=115200)
    parser.add_argument("--rates", default="115200,460800,921600,1000000")
    parser.add_argument("--duration", type=float, default=10.0)
    parser.add_argument("--performance-payload-bytes", type=int, default=1024)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--board", default="unknown")
    parser.add_argument("--tester", default="unknown")
    parser.add_argument("--firmware-version", default="unknown")
    parser.add_argument("--firmware-commit", default="unknown")
    parser.add_argument("--results-file", type=Path, default=DEFAULT_RESULTS_FILE)
    parser.add_argument("--skip-functional", action="store_true")
    parser.add_argument("--skip-performance", action="store_true")
    parser.add_argument("--continue-after-functional-failure", action="store_true")
    parser.add_argument("--no-record", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.skip_functional and arguments.skip_performance:
        print("at least one test phase must run", file=sys.stderr)
        return 2
    if not arguments.skip_functional and (not arguments.pico_cdc1 or not arguments.pico_cdc4):
        print("--pico-cdc1 and --pico-cdc4 are required for the functional test", file=sys.stderr)
        return 2

    functional = None
    performance = None
    if not arguments.skip_functional:
        functional = run_child("functional test", build_functional_command(arguments))

    if not arguments.skip_performance and (
            functional is None or functional[0] == 0 or arguments.continue_after_functional_failure):
        performance = run_child("performance test", build_performance_command(arguments))
    elif not arguments.skip_performance:
        print("SKIP performance test: functional test failed", file=sys.stderr)

    timestamp = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    raw_log = None
    if not arguments.no_record:
        raw_log = write_raw_log(
            arguments.results_file.resolve(), timestamp,
            "\n\n".join(result[1] for result in (functional, performance)
                           if result is not None))
    entry = format_result_entry(arguments, timestamp, functional, performance, raw_log)
    if not arguments.no_record:
        prepend_result(arguments.results_file.resolve(), entry)
        print(f"Recorded result in {arguments.results_file}")

    codes = [code for result in (functional, performance) if result is not None for code in [result[0]]]
    return 0 if codes and all(code == 0 for code in codes) else 1


if __name__ == "__main__":
    raise SystemExit(main())

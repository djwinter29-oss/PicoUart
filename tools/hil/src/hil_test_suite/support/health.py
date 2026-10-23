#!/usr/bin/env python3
"""HID health collection shared by physical hardware test runners."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import time
from pico_uart import PicoUartHid
from .paths import REPO_ROOT

HOST_PYTHON_SRC = REPO_ROOT / "host/python/src"
HID_MODULE = "pico_uart"


def _run_hid(*arguments: str) -> subprocess.CompletedProcess[str]:
    python_path = os.pathsep.join(part for part in (str(HOST_PYTHON_SRC), os.environ.get("PYTHONPATH", "")) if part)
    environment = {**os.environ, "PYTHONPATH": python_path}
    return subprocess.run(
        [sys.executable, "-m", HID_MODULE, *arguments],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        env=environment,
    )


def _collect_hid_health_once() -> dict:
    """Read all six non-consuming Feature snapshots; never parse human console output."""
    snapshot = {"channels": {}, "overruns": {}, "diagnostics": {}, "firmware_version": None, "error": None}
    try:
        with PicoUartHid() as board:
            snapshot["firmware_version"] = board.read_firmware_version()
            for index in range(6):
                snapshot["channels"][index] = board.read_channel(index)["state"]
                diagnostic = board.read_channel_diagnostic(index)
                snapshot["diagnostics"][index] = diagnostic
                snapshot["overruns"][index] = diagnostic["rx_overrun_count"]
    except (OSError, RuntimeError) as error:
        snapshot["error"] = f"HID health query failed: {error}"
    return snapshot


def collect_hid_health() -> dict:
    """Collect HID health, retrying transient post-reset enumeration failures."""
    snapshot = _collect_hid_health_once()
    for _ in range(2):
        if not snapshot.get("error"):
            break
        time.sleep(0.5)
        snapshot = _collect_hid_health_once()
    return snapshot


def read_system_clock_hz() -> int | None:
    """Read the live system clock through the board's read-only HID command."""
    result = _run_hid("hardware")
    if result.returncode != 0:
        return None
    try:
        hardware = json.loads(result.stdout)
    except (TypeError, ValueError):
        return None
    clock_hz = hardware.get("system_clock_hz") if isinstance(hardware, dict) else None
    if isinstance(clock_hz, int) and not isinstance(clock_hz, bool) and clock_hz > 0:
        return clock_hz
    return None


def health_is_clean(snapshot: dict | None, baseline: dict | None = None) -> bool:
    """Return true when HID is available and no health/overflow criteria failed."""
    if snapshot is None or snapshot.get("error"):
        return False
    indices = set(range(6))
    if any(set(snapshot.get(field, {})) != indices for field in ("channels", "overruns", "diagnostics")):
        return False
    for index, state in snapshot["channels"].items():
        if state not in ("closed", "ready"):
            return False
        diagnostic = snapshot["diagnostics"][index]
        if (diagnostic["uart_measurement_state"] != 1 or diagnostic["rx_overrun_count"] != 0
                or diagnostic["rx_error_count"] != 0 or diagnostic["last_control_error_reason"] != 0):
            return False
    if baseline is not None:
        if baseline.get("error"):
            return False
        for index, count in snapshot["overruns"].items():
            if count != baseline["overruns"].get(index):
                return False
    return True


def health_summary(snapshot: dict | None) -> str:
    """Format a compact health status for the result log."""
    if snapshot is None:
        return "not collected"
    if snapshot.get("error"):
        return "ERROR: " + snapshot["error"]
    channels = ", ".join(f"cdc{index}={state}" for index, state in sorted(snapshot["channels"].items()))
    overruns = ", ".join(f"cdc{index}={count}" for index, count in sorted(snapshot["overruns"].items()))
    firmware_version = snapshot.get("firmware_version") or "unknown"
    return f"health [{channels}]; overruns [{overruns}]; firmware={firmware_version}"


def health_evidence(snapshot: dict | None) -> str:
    """Return the raw HID command evidence used by a health decision."""
    if snapshot is None:
        return "HID health: not collected\n"
    return (
        f"HID health summary: {health_summary(snapshot)}\n"
        f"HID Feature snapshots: {json.dumps(snapshot, sort_keys=True)}\n"
    )

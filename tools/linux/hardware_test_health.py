#!/usr/bin/env python3
"""HID health collection shared by physical hardware test runners."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
HID_TOOL = REPO_ROOT / "host/python/src/pico_uart_hid.py"
HEALTH_PATTERN = re.compile(r"cdc([0-5]) health=0x([0-9a-fA-F]+)\[[^]]*\]")
OVERRUN_PATTERN = re.compile(r"cdc([0-5])=([0-9]+)")
BAD_HEALTH_BITS = 0xCC  # control_error, control_pending, rx_overrun, rx_error


def collect_hid_health() -> dict:
    """Collect one HID monitor sample and overflow snapshot."""
    monitor = subprocess.run(
        [sys.executable, str(HID_TOOL), "monitor", "--duration", "1"],
        cwd=REPO_ROOT, capture_output=True, text=True)
    overruns = subprocess.run(
        [sys.executable, str(HID_TOOL), "overruns"],
        cwd=REPO_ROOT, capture_output=True, text=True)
    output = monitor.stdout + monitor.stderr
    overflow_output = overruns.stdout + overruns.stderr
    channels = {int(index): int(flags, 16)
                for index, flags in HEALTH_PATTERN.findall(output)}
    overflow_counts = {int(index): int(value)
                       for index, value in OVERRUN_PATTERN.findall(overflow_output)}
    errors = []
    if monitor.returncode != 0:
        errors.append(f"HID monitor failed: {output.strip()}")
    if overruns.returncode != 0:
        errors.append(f"HID overflow query failed: {overflow_output.strip()}")
    if len(channels) != 6:
        errors.append(f"HID monitor returned {len(channels)}/6 channel samples")
    if len(overflow_counts) != 6:
        errors.append(f"HID overflow query returned {len(overflow_counts)}/6 channels")
    return {
        "channels": channels,
        "overruns": overflow_counts,
        "error": "; ".join(errors) if errors else None,
    }


def health_is_clean(snapshot: dict | None, baseline: dict | None = None) -> bool:
    """Return true when HID is available and no health/overflow criteria failed."""
    if snapshot is None or snapshot.get("error"):
        return False
    if len(snapshot["channels"]) != 6 or len(snapshot["overruns"]) != 6:
        return False
    if any(flags & BAD_HEALTH_BITS for flags in snapshot["channels"].values()):
        return False
    if baseline is not None:
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
    channels = ", ".join(f"cdc{index}=0x{flags:02x}"
                          for index, flags in sorted(snapshot["channels"].items()))
    overruns = ", ".join(f"cdc{index}={count}"
                          for index, count in sorted(snapshot["overruns"].items()))
    return f"health [{channels}]; overruns [{overruns}]"

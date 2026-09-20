#!/usr/bin/env python3
"""HID health collection shared by physical hardware test runners."""

from __future__ import annotations

import re
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
HID_TOOL = REPO_ROOT / "host/python/src/pico_uart_hid.py"
HEALTH_PATTERN = re.compile(r"cdc([0-5]) health=0x([0-9a-fA-F]+)\[[^]]*\]")
OVERRUN_PATTERN = re.compile(r"cdc([0-5])=([0-9]+)")
READY_BIT = 1 << 0
BAD_HEALTH_BITS = 0xCE  # init_failed, control_error, control_pending, rx_overrun, rx_error


def _collect_hid_health_once() -> dict:
    """Collect one HID monitor sample and overflow snapshot."""
    monitor = subprocess.run(
        [sys.executable, str(HID_TOOL), "monitor", "--duration", "1"],
        cwd=REPO_ROOT, capture_output=True, text=True)
    overruns = subprocess.run(
        [sys.executable, str(HID_TOOL), "overruns"],
        cwd=REPO_ROOT, capture_output=True, text=True)
    version = subprocess.run(
        [sys.executable, str(HID_TOOL), "version"],
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
    if version.returncode != 0:
        errors.append(f"HID version query failed: {(version.stdout + version.stderr).strip()}")
    if len(channels) != 6:
        errors.append(f"HID monitor returned {len(channels)}/6 channel samples")
    if len(overflow_counts) != 6:
        errors.append(f"HID overflow query returned {len(overflow_counts)}/6 channels")
    return {
        "channels": channels,
        "overruns": overflow_counts,
        "firmware_version": version.stdout.strip() if version.returncode == 0 else None,
        "monitor_returncode": monitor.returncode,
        "overruns_returncode": overruns.returncode,
        "version_returncode": version.returncode,
        "monitor_output": output,
        "overruns_output": overflow_output,
        "version_output": version.stdout + version.stderr,
        "error": "; ".join(errors) if errors else None,
    }


def collect_hid_health() -> dict:
    """Collect HID health, retrying transient post-reset enumeration failures."""
    snapshot = _collect_hid_health_once()
    for _ in range(2):
        if not snapshot.get("error"):
            break
        time.sleep(0.5)
        snapshot = _collect_hid_health_once()
    return snapshot


def health_is_clean(snapshot: dict | None, baseline: dict | None = None) -> bool:
    """Return true when HID is available and no health/overflow criteria failed."""
    if snapshot is None or snapshot.get("error"):
        return False
    if len(snapshot["channels"]) != 6 or len(snapshot["overruns"]) != 6:
        return False
    if any(not flags & READY_BIT or flags & BAD_HEALTH_BITS
           for flags in snapshot["channels"].values()):
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
    channels = ", ".join(f"cdc{index}=0x{flags:02x}"
                          for index, flags in sorted(snapshot["channels"].items()))
    overruns = ", ".join(f"cdc{index}={count}"
                          for index, count in sorted(snapshot["overruns"].items()))
    firmware_version = snapshot.get("firmware_version") or "unknown"
    return f"health [{channels}]; overruns [{overruns}]; firmware={firmware_version}"


def health_evidence(snapshot: dict | None) -> str:
    """Return the raw HID command evidence used by a health decision."""
    if snapshot is None:
        return "HID health: not collected\n"
    return (f"HID health summary: {health_summary(snapshot)}\n"
            f"HID monitor exit={snapshot.get('monitor_returncode')}\n"
            f"{snapshot.get('monitor_output', '')}"
            f"HID overruns exit={snapshot.get('overruns_returncode')}\n"
            f"{snapshot.get('overruns_output', '')}"
            f"HID version exit={snapshot.get('version_returncode')}\n"
            f"{snapshot.get('version_output', '')}")

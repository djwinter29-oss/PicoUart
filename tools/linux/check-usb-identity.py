#!/usr/bin/env python3
"""Fail a release when firmware still uses the lab USB placeholder identity.

Lab trees may keep cafe:4010. Tag/publish jobs must pass --allow-lab (or set
ALLOW_LAB_USB_IDENTITY) to keep that placeholder; otherwise the job fails so a
public release cannot ship the development VID/PID by accident.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _truthy(value: str | None) -> bool:
    if value is None:
        return False
    return value.strip().lower() in {"1", "true", "yes", "on"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--allow-lab",
        action="store_true",
        help="Allow cafe:4010 (lab/dev images). Also honored via ALLOW_LAB_USB_IDENTITY.",
    )
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=_repo_root(),
        help="Repository root containing firmware/src/config/usb_identity.h",
    )
    args = parser.parse_args()

    sys.path.insert(0, str(args.repo_root / "host" / "python" / "tests"))
    from contract import firmware_usb_ids, is_lab_placeholder_identity

    vid, pid = firmware_usb_ids(args.repo_root)
    allow_lab = args.allow_lab or _truthy(os.environ.get("ALLOW_LAB_USB_IDENTITY"))

    print(f"USB identity {vid:#06x}:{pid:#06x}")
    if not is_lab_placeholder_identity(vid, pid):
        print("Allocated identity; release identity gate passed.")
        return 0

    if allow_lab:
        print(
            "Lab placeholder cafe:4010 allowed by --allow-lab / ALLOW_LAB_USB_IDENTITY. "
            "Do not promote a public GitHub Release on this identity."
        )
        return 0

    print(
        "Refusing lab USB placeholder cafe:4010 for a production release.\n"
        "Allocate a VID/PID (pid.codes or USB-IF), update usb_identity.h and "
        "host/python/src/pico_uart_hid.py, or rerun with --allow-lab / "
        "ALLOW_LAB_USB_IDENTITY=true for lab-only images.\n"
        "See docs/releasing.md and SECURITY.md.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

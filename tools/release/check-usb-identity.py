#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Report the USB identity configured in firmware."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=_repo_root(),
        help="Repository root containing firmware/src/config/usb_identity.h",
    )
    args = parser.parse_args()

    from contract import firmware_usb_ids, is_lab_placeholder_identity

    vid, pid = firmware_usb_ids(args.repo_root)
    print(f"USB identity {vid:#06x}:{pid:#06x}")
    allocated_identity = (0x2E8A, 0x116B)
    if (vid, pid) == allocated_identity:
        print("Allocated USB identity configured.")
        return 0

    if is_lab_placeholder_identity(vid, pid):
        print("The retired cafe:4010 development identity is no longer approved for PicoUart.", file=sys.stderr)
    else:
        print(
            f"USB identity {vid:#06x}:{pid:#06x} does not match the allocated "
            f"PicoUart identity {allocated_identity[0]:#06x}:{allocated_identity[1]:#06x}.",
            file=sys.stderr,
        )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())

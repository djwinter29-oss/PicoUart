#!/usr/bin/env python3
"""Report whether firmware uses the project's unallocated lab USB identity."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
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
    print(f"USB identity {vid:#06x}:{pid:#06x}")
    if not is_lab_placeholder_identity(vid, pid):
        print("Allocated USB identity configured.")
        return 0

    print(
        "Using cafe:4010, this repository's unallocated lab USB identity. "
        "It is permitted for published PicoUart project artifacts but must not be reused "
        "for commercial devices. See docs/releasing.md and SECURITY.md."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Parse firmware headers for host/firmware contract checks."""

from __future__ import annotations

import re
from pathlib import Path


def parse_c_u_define(text: str, name: str) -> int:
    """Parse `#define NAME <integer>u` / hex forms from a C header."""
    pattern = rf"^\s*#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+|\d+)u?\s*$"
    for line in text.splitlines():
        match = re.match(pattern, line)
        if match:
            return int(match.group(1), 0)
    raise ValueError(f"define {name} not found")


def firmware_usb_ids(repo_root: Path) -> tuple[int, int]:
    text = (repo_root / "firmware" / "src" / "config" / "usb_identity.h").read_text(
        encoding="utf-8"
    )
    return (
        parse_c_u_define(text, "PICO_UART_USB_VID"),
        parse_c_u_define(text, "PICO_UART_USB_PID"),
    )


def firmware_hid_report_version(repo_root: Path) -> int:
    text = (repo_root / "firmware" / "src" / "usb" / "usb_hid.c").read_text(encoding="utf-8")
    return parse_c_u_define(text, "USB_HID_REPORT_VERSION")


def usb_identity_needle_le(vid: int, pid: int, bcd_device: int = 0) -> bytes:
    """Little-endian idVendor, idProduct, bcdDevice as they appear in descriptors."""
    return bytes(
        (
            vid & 0xFF,
            (vid >> 8) & 0xFF,
            pid & 0xFF,
            (pid >> 8) & 0xFF,
            bcd_device & 0xFF,
            (bcd_device >> 8) & 0xFF,
        )
    )

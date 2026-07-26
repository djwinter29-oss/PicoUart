"""Parse firmware headers for host/firmware contract checks."""

from __future__ import annotations

import re
from pathlib import Path


def parse_c_u_define(text: str, name: str) -> int:
    """Parse `#define NAME <integer>u` / hex forms from a C header or .c file."""
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


def firmware_hid_constants(repo_root: Path) -> dict[str, int]:
    text = (repo_root / "firmware" / "src" / "usb" / "usb_hid.c").read_text(encoding="utf-8")
    names = (
        "USB_HID_REPORT_VERSION",
        "USB_HID_REPORT_ID_STATUS",
        "USB_HID_REPORT_ID_BOARD_STATUS",
        "USB_HID_REPORT_ID_COMMAND",
        "USB_HID_COMMAND_TOGGLE_LED",
        "USB_HID_COMMAND_RESET_BOARD",
        "USB_HID_COMMAND_ARM_RESET",
        "USB_HID_RESET_ARM_WINDOW_MS",
    )
    constants = {name: parse_c_u_define(text, name) for name in names}
    match = re.search(r"^\s*#define\s+USB_HID_SIGNATURE0\s+'([^']+)'\s*$", text, flags=re.MULTILINE)
    if not match or len(match.group(1)) != 1:
        raise ValueError("USB_HID_SIGNATURE0 character literal not found")
    constants["USB_HID_SIGNATURE0"] = ord(match.group(1))
    return constants


def firmware_hid_report_version(repo_root: Path) -> int:
    return firmware_hid_constants(repo_root)["USB_HID_REPORT_VERSION"]


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


def is_lab_placeholder_identity(vid: int, pid: int) -> bool:
    return (vid, pid) == (0xCAFE, 0x4010)


def firmware_hid_report_count(repo_root: Path, report_id: int) -> int:
    """Parse Report Count for a HID report ID from usb_descriptors.c."""
    text = (repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c").read_text(
        encoding="utf-8"
    )
    # Match the Report ID item, then the following Report Count in the same report.
    match = re.search(
        rf"0x85,\s*0x{report_id:02X}\s*,.*?0x95,\s*(0x[0-9A-Fa-f]+)",
        text,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not match:
        raise ValueError(f"HID report count for report ID {report_id} not found")
    return int(match.group(1), 0)


def firmware_hid_status_report_count(repo_root: Path) -> int:
    """Parse Report Count for HID input report ID 1 from usb_descriptors.c."""
    return firmware_hid_report_count(repo_root, 1)

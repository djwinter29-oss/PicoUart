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


def firmware_hid_reset_default_enabled(repo_root: Path) -> bool:
    """Return whether HID reset is enabled by the default #define in usb_hid.c."""
    text = (repo_root / "firmware" / "src" / "usb" / "usb_hid.c").read_text(encoding="utf-8")
    match = re.search(
        r"#ifndef\s+PICO_UART_ALLOW_HID_RESET\s*\n\s*#define\s+PICO_UART_ALLOW_HID_RESET\s+(\d+)",
        text,
    )
    if not match:
        raise ValueError("PICO_UART_ALLOW_HID_RESET default not found")
    return int(match.group(1)) != 0


def firmware_uart_board_ports(repo_root: Path) -> list[dict[str, object]]:
    """Parse TX/RX pins and HW flow-control defaults from uart_board.c."""
    text = (repo_root / "firmware" / "src" / "config" / "uart_board.c").read_text(
        encoding="utf-8"
    )
    ports: list[dict[str, object]] = []
    for match in re.finditer(
        r"\.info\s*=\s*\{(UART_PORT_\d+)\s*,\s*(UART_DRIVER_BACKEND_\w+)\s*,"
        r"\s*PICO_UART_BOARD_DEFAULT_BAUD_RATE\s*,\s*(\d+)u\s*,\s*(\d+)u\}"
        r"(.*?)\n\s*\},",
        text,
        flags=re.DOTALL,
    ):
        port_name, backend, tx_pin, rx_pin, body = match.groups()
        entry: dict[str, object] = {
            "id": port_name,
            "backend": backend,
            "tx_pin": int(tx_pin),
            "rx_pin": int(rx_pin),
        }
        if backend == "UART_DRIVER_BACKEND_HW":
            hw = re.search(
                r"\.backend\.hw\s*=\s*\{[^;]*?"
                r"(\d+)u\s*,\s*(\d+)u\s*,\s*(\d+)u\s*,\s*(\d+)u\s*,\s*(true|false)",
                body,
                flags=re.DOTALL,
            )
            if not hw:
                raise ValueError(f"HW backend fields not found for {port_name}")
            if int(hw.group(1)) != entry["tx_pin"] or int(hw.group(2)) != entry["rx_pin"]:
                raise ValueError(f"HW TX/RX mismatch for {port_name}")
            entry["cts_pin"] = int(hw.group(3))
            entry["rts_pin"] = int(hw.group(4))
            entry["hardware_flow_control"] = hw.group(5) == "true"
        ports.append(entry)
    if len(ports) != 6:
        raise ValueError(f"expected 6 uart_board ports, found {len(ports)}")
    return ports

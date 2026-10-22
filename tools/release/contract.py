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


def firmware_usb_product_string(repo_root: Path) -> str:
    """Parse the shared USB/HID product string from usb_identity.h."""
    text = (repo_root / "firmware" / "src" / "config" / "usb_identity.h").read_text(encoding="utf-8")
    match = re.search(
        r'^\s*#define\s+PICO_UART_USB_PRODUCT\s+"([^"]+)"\s*$',
        text,
        flags=re.MULTILINE,
    )
    if not match:
        raise ValueError("PICO_UART_USB_PRODUCT not found")
    return match.group(1)


def firmware_cdc_interface_strings(repo_root: Path) -> list[str]:
    """Parse CDC interface string literals from usb_descriptors.c."""
    text = (repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c").read_text(encoding="utf-8")
    match = re.search(
        r"static char const \*string_desc_arr\[\] = \{(.*?)\};",
        text,
        flags=re.DOTALL,
    )
    if not match:
        raise ValueError("USB string_desc_arr not found")
    strings = re.findall(r'"([^"]+)"', match.group(1))
    cdc = [value for value in strings if value.startswith("CDC")]
    if len(cdc) != 6:
        raise ValueError(f"expected 6 CDC interface strings, found {cdc!r}")
    return cdc


def firmware_usb_ids(repo_root: Path) -> tuple[int, int]:
    text = (repo_root / "firmware" / "src" / "config" / "usb_identity.h").read_text(encoding="utf-8")
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
        "USB_HID_REPORT_ID_HARDWARE_INFO",
        "USB_HID_HARDWARE_INFO_VERSION",
        "USB_HID_MCU_RP2040",
        "USB_HID_MCU_RP2350",
        "USB_HID_REPORT_ID_IDENTITY",
        "USB_HID_IDENTITY_VERSION",
        "USB_HID_REPORT_ID_TELEMETRY",
        "USB_HID_TELEMETRY_VERSION",
        "USB_HID_REPORT_ID_CONFIGURATION",
        "USB_HID_REPORT_ID_CONFIGURATION_SAVE",
        "USB_HID_REPORT_ID_CHANNEL_BASE",
        "USB_HID_COMMAND_TOGGLE_LED",
        "USB_HID_COMMAND_RESET_BOARD",
        "USB_HID_COMMAND_ARM_RESET",
        "USB_HID_RESET_ARM_WINDOW_MS",
        "USB_HID_BOARD_STATUS_FLAG_HID_RESET",
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
    text = (repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c").read_text(encoding="utf-8")
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


def firmware_hid_reset_cmake_default_enabled(repo_root: Path) -> bool:
    """Return whether the firmware CMake option enables HID reset by default."""
    text = (repo_root / "firmware" / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r'option\(\s*PICO_UART_ALLOW_HID_RESET\s+"[^"]*"\s+(ON|OFF)\s*\)',
        text,
    )
    if not match:
        raise ValueError("PICO_UART_ALLOW_HID_RESET CMake option default not found")
    return match.group(1) == "ON"


def firmware_hid_interface_number(repo_root: Path) -> int:
    """Return ITF_NUM_HID from the configuration-descriptor interface enum.

    ITF_NUM_CDC0 is explicitly 0. Later enumerators increment by one, so the
    HID interface number is its position in usb_interface_number_t.
    """
    text = (repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c").read_text(encoding="utf-8")
    match = re.search(
        r"typedef enum \{(?P<body>.*?)\}\s*usb_interface_number_t\s*;",
        text,
        flags=re.DOTALL,
    )
    if not match:
        raise ValueError("usb_interface_number_t not found")
    if not re.search(r"\bITF_NUM_CDC0\s*=\s*0\b", match.group("body")):
        raise ValueError("ITF_NUM_CDC0 must be explicitly 0")
    names = re.findall(r"\b(ITF_NUM_[A-Z0-9_]+)\b", match.group("body"))
    if "ITF_NUM_HID" not in names:
        raise ValueError("ITF_NUM_HID not found")
    return names.index("ITF_NUM_HID")


def firmware_uart_board_ports(repo_root: Path) -> list[dict[str, object]]:
    """Parse common pin assignments and HW descriptor flow-control defaults."""
    text = (repo_root / "firmware/src/config/uart_config.c").read_text(encoding="utf-8")
    hardware = (repo_root / "firmware/src/uart/hw/hw_uart_board_config.c").read_text(encoding="utf-8")
    ports: list[dict[str, object]] = []
    for match in re.finditer(
        r"\{\s*\.id\s*=\s*(UART_PORT_\d+)(.*?)\n\s*\},",
        text,
        flags=re.DOTALL,
    ):
        port_name, info_body = match.groups()
        backend_match = re.search(r"\.backend\s*=\s*(UART_DRIVER_BACKEND_\w+)", info_body)
        if backend_match is None:
            raise ValueError(f"backend not found for {port_name}")
        backend = backend_match.group(1)
        entry: dict[str, object] = {
            "id": port_name,
            "backend": backend,
        }
        for field in ("tx_pin", "rx_pin", "cts_pin", "rts_pin"):
            pin = re.search(rf"\.{field}\s*=\s*(\d+)u", info_body)
            if pin is None:
                raise ValueError(f"{field} not found for {port_name}")
            entry[field] = int(pin.group(1))
        if backend == "UART_DRIVER_BACKEND_HW":
            descriptor = re.search(rf"\[{re.escape(port_name)}\]\s*=\s*\{{([^{{}}]*)\}}", hardware)
            flow = re.search(r"\.hardware_flow_control\s*=\s*(true|false)", descriptor.group(1)) if descriptor else None
            if flow is None:
                raise ValueError(f"HW flow-control setting not found for {port_name}")
            entry["hardware_flow_control"] = flow.group(1) == "true"
        ports.append(entry)
    if len(ports) != 6:
        raise ValueError(f"expected 6 uart_board ports, found {len(ports)}")
    return ports

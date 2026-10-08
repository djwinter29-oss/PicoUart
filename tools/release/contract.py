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
    """Parse the USB product string from usb_descriptors.c."""
    text = (repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c").read_text(
        encoding="utf-8"
    )
    match = re.search(
        r'^\s*#define\s+USB_STR_PRODUCT\s+"([^"]+)"\s*$',
        text,
        flags=re.MULTILINE,
    )
    if not match:
        raise ValueError("USB_STR_PRODUCT not found")
    return match.group(1)


def firmware_cdc_interface_strings(repo_root: Path) -> list[str]:
    """Parse CDC interface string literals from usb_descriptors.c."""
    text = (repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c").read_text(
        encoding="utf-8"
    )
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
        "USB_HID_REPORT_ID_OVERFLOW_COUNTS",
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
    text = (repo_root / "firmware" / "src" / "usb" / "usb_descriptors.c").read_text(
        encoding="utf-8"
    )
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
    """Parse TX/RX pins and HW flow-control defaults from uart_board.c."""
    text = (repo_root / "firmware" / "src" / "board" / "uart_board.c").read_text(
        encoding="utf-8"
    )
    ports: list[dict[str, object]] = []
    for match in re.finditer(
        r"\{\s*\.info\s*=\s*\{(.*?)\n\s*\},\s*"
        r"\.backend\.(hw|pio)\s*=\s*\{(.*?)\n\s*\},\s*\n\s*\},",
        text,
        flags=re.DOTALL,
    ):
        info_body, backend_kind, backend_body = match.groups()
        id_match = re.search(r"\.id\s*=\s*(UART_PORT_\d+)", info_body)
        backend_match = re.search(
            r"\.backend\s*=\s*(UART_DRIVER_BACKEND_\w+)", info_body
        )
        tx_match = re.search(r"\.tx_pin\s*=\s*(\d+)u", info_body)
        rx_match = re.search(r"\.rx_pin\s*=\s*(\d+)u", info_body)
        if not all((id_match, backend_match, tx_match, rx_match)):
            raise ValueError("incomplete uart_board port info initializer")

        port_name = id_match.group(1)
        backend = backend_match.group(1)
        tx_pin = tx_match.group(1)
        rx_pin = rx_match.group(1)
        entry: dict[str, object] = {
            "id": port_name,
            "backend": backend,
            "tx_pin": int(tx_pin),
            "rx_pin": int(rx_pin),
        }
        if backend == "UART_DRIVER_BACKEND_HW":
            hw = re.search(
                r"\.cts_pin\s*=\s*(\d+)u.*?"
                r"\.rts_pin\s*=\s*(\d+)u.*?"
                r"\.hardware_flow_control\s*=\s*(true|false)",
                backend_body,
                flags=re.DOTALL,
            )
            if not hw:
                raise ValueError(f"HW backend fields not found for {port_name}")
            backend_tx = re.search(r"\.tx_pin\s*=\s*(\d+)u", backend_body)
            backend_rx = re.search(r"\.rx_pin\s*=\s*(\d+)u", backend_body)
            if (backend_tx is None) or (backend_rx is None):
                raise ValueError(f"HW TX/RX fields not found for {port_name}")
            if int(backend_tx.group(1)) != entry["tx_pin"] or int(backend_rx.group(1)) != entry["rx_pin"]:
                raise ValueError(f"HW TX/RX mismatch for {port_name}")
            entry["cts_pin"] = int(hw.group(1))
            entry["rts_pin"] = int(hw.group(2))
            entry["hardware_flow_control"] = hw.group(3) == "true"
        ports.append(entry)
    if len(ports) != 6:
        raise ValueError(f"expected 6 uart_board ports, found {len(ports)}")
    return ports

#!/usr/bin/env python3
"""Validate PicoUart artifacts and compiled USB descriptor contracts."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def _device_descriptors(data: bytes) -> list[tuple[int, ...]]:
    descriptors = []
    for offset in range(len(data) - 17):
        if data[offset : offset + 2] != b"\x12\x01":
            continue
        descriptor = struct.unpack_from("<BBHBBBBHHHBBBB", data, offset)
        if descriptor[2] == 0x0200 and descriptor[6] in (8, 16, 32, 64):
            descriptors.append(descriptor)
    return descriptors


def _configuration_descriptors(data: bytes) -> list[tuple[int, list[bytes]]]:
    candidates = []
    for offset in range(len(data) - 8):
        if data[offset : offset + 2] != b"\x09\x02":
            continue
        total_length = int.from_bytes(data[offset + 2 : offset + 4], "little")
        if total_length < 9 or offset + total_length > len(data):
            continue
        descriptors = []
        cursor = offset
        end = offset + total_length
        while cursor < end:
            length = data[cursor]
            if length < 2 or cursor + length > end:
                break
            descriptors.append(data[cursor : cursor + length])
            cursor += length
        if cursor == end:
            candidates.append((data[offset + 4], descriptors))
    return candidates


def _endpoint_contract(descriptors: list[bytes]) -> list[tuple[int, int, int, int]]:
    contract = []
    interface_number = -1
    for descriptor in descriptors:
        if descriptor[1] == 4 and len(descriptor) >= 9:
            interface_number = descriptor[2]
        elif descriptor[1] == 5 and len(descriptor) >= 7:
            contract.append(
                (
                    interface_number,
                    descriptor[2],
                    descriptor[3] & 0x03,
                    int.from_bytes(descriptor[4:6], "little"),
                )
            )
    return contract


def _hid_reports(data: bytes, length: int) -> list[dict[int, tuple[int, int, int]]]:
    candidates = []
    for offset in range(len(data) - length + 1):
        report_id = None
        report_count = None
        report_size = None
        reports: dict[int, tuple[int, int, int]] = {}
        cursor = offset
        end = offset + length
        valid = True
        while cursor < end:
            prefix = data[cursor]
            cursor += 1
            if prefix == 0xFE:
                valid = False
                break
            size = (0, 1, 2, 4)[prefix & 0x03]
            if cursor + size > end:
                valid = False
                break
            value = int.from_bytes(data[cursor : cursor + size], "little")
            cursor += size
            item_type = (prefix >> 2) & 0x03
            tag = prefix >> 4
            if item_type == 1 and tag == 8:
                report_id = value
            elif item_type == 1 and tag == 9:
                report_count = value
            elif item_type == 1 and tag == 7:
                report_size = value
            elif item_type == 0 and tag in (8, 11):
                if report_id is None or report_count is None or report_size is None:
                    valid = False
                    break
                reports[report_id] = (report_count, report_size, tag)
        if valid and cursor == end and data[end - 1] == 0xC0:
            candidates.append(reports)
    return candidates


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--version", default="0.0.0-dev")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()

    sys.path.insert(0, str(args.repo_root / "host" / "python" / "tests"))
    from contract import (  # pylint: disable=import-error,import-outside-toplevel
        firmware_hid_constants,
        firmware_hid_report_count,
        firmware_usb_ids,
    )

    artifacts = {ext: args.build_dir / f"pico_uart.{ext}" for ext in ("bin", "elf", "uf2", "hex")}
    missing = [str(path) for path in artifacts.values() if not path.is_file()]
    if missing:
        raise SystemExit(f"missing build artifacts: {', '.join(missing)}")

    elf = artifacts["elf"].read_bytes()
    if elf[:4] != b"\x7fELF" or len(elf) < 20:
        raise SystemExit("pico_uart.elf is not an ELF file")
    byte_order = "little" if elf[5] == 1 else "big"
    if int.from_bytes(elf[18:20], byte_order) != 40:
        raise SystemExit("pico_uart.elf is not an ARM executable")
    if not artifacts["uf2"].read_bytes().startswith(b"UF2\nWQ]"):
        raise SystemExit("pico_uart.uf2 has an invalid UF2 header")
    if not artifacts["hex"].read_bytes().startswith(b":"):
        raise SystemExit("pico_uart.hex is not Intel HEX")

    data = artifacts["bin"].read_bytes()
    vid, pid = firmware_usb_ids(args.repo_root)
    core_version = args.version.split("-", 1)[0]
    major, minor, _patch = (int(part) for part in core_version.split("."))
    bcd_device = ((major // 10) << 12) | ((major % 10) << 8) | ((minor // 10) << 4) | (minor % 10)
    matching_devices = [
        descriptor
        for descriptor in _device_descriptors(data)
        if descriptor[7:10] == (vid, pid, bcd_device)
        and descriptor[3:6] == (0xEF, 0x02, 0x01)
        and descriptor[13] == 1
    ]
    if len(matching_devices) != 1:
        raise SystemExit(f"expected one matching USB device descriptor, found {len(matching_devices)}")

    matching_configs = []
    hid_report_lengths = []
    expected_endpoints = []
    for cdc_index in range(6):
        control_interface = cdc_index * 2
        data_interface = control_interface + 1
        expected_endpoints.append((control_interface, 0x81 + cdc_index * 2, 3, 8))
        expected_endpoints.append((data_interface, 0x02 + cdc_index * 2, 2, 64))
        expected_endpoints.append((data_interface, 0x82 + cdc_index * 2, 2, 64))
    expected_endpoints.append((12, 0x8E, 3, 64))
    for interface_count, descriptors in _configuration_descriptors(data):
        interfaces = [descriptor for descriptor in descriptors if descriptor[1] == 4 and len(descriptor) >= 9]
        classes = [descriptor[5] for descriptor in interfaces]
        interface_numbers = [descriptor[2] for descriptor in interfaces]
        iad_count = sum(descriptor[1] == 0x0B for descriptor in descriptors)
        endpoints = _endpoint_contract(descriptors)
        if (
            interface_count == 13
            and interface_numbers == list(range(13))
            and classes.count(0x02) == 6
            and classes.count(0x0A) == 6
            and classes.count(0x03) == 1
            and iad_count == 6
            and endpoints == expected_endpoints
        ):
            matching_configs.append(descriptors)
            hid_report_lengths.extend(
                int.from_bytes(descriptor[7:9], "little")
                for descriptor in descriptors
                if descriptor[1] == 0x21 and len(descriptor) >= 9 and descriptor[6] == 0x22
            )
    if len(matching_configs) != 1 or len(hid_report_lengths) != 1:
        raise SystemExit("expected one 6xCDC/1xHID USB configuration descriptor")

    constants = firmware_hid_constants(args.repo_root)
    expected_reports = {
        constants["USB_HID_REPORT_ID_STATUS"]: (firmware_hid_report_count(args.repo_root, 1), 8, 8),
        constants["USB_HID_REPORT_ID_BOARD_STATUS"]: (firmware_hid_report_count(args.repo_root, 3), 8, 11),
        constants["USB_HID_REPORT_ID_COMMAND"]: (firmware_hid_report_count(args.repo_root, 4), 8, 11),
        constants["USB_HID_REPORT_ID_OVERFLOW_COUNTS"]: (firmware_hid_report_count(args.repo_root, 5), 8, 11),
    }
    matching_reports = [
        reports for reports in _hid_reports(data, hid_report_lengths[0]) if reports == expected_reports
    ]
    if len(matching_reports) != 1:
        raise SystemExit("compiled HID report descriptor does not match the firmware/host contract")

    if (args.version.encode("ascii") + b"\0") not in data:
        raise SystemExit(f"firmware version string {args.version!r} is missing")

    print(
        f"Verified {args.build_dir}: ARM artifacts, USB {vid:#06x}:{pid:#06x}, "
        f"bcdDevice=0x{bcd_device:04x}, 6xCDC/1xHID, HID reports {sorted(expected_reports)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

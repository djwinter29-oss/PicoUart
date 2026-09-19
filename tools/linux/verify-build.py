#!/usr/bin/env python3
"""Validate PicoUart artifacts and compiled USB descriptor contracts."""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_NOT_MAIN_FLASH = 0x00000001
UF2_FLAG_FAMILY_ID_PRESENT = 0x00002000
UF2_FLAG_EXTENSION_FLAGS_PRESENT = 0x00008000
UF2_EXTENSION_RP2_IGNORE_BLOCK = 0x9957E304
PICO_FLASH_BASE = 0x10000000
RP2350_ABSOLUTE_BLOCK_ADDRESS = 0x10FFFF00
UF2_FAMILY_RP2350_ABSOLUTE = 0xE48BFF57
UF2_FAMILY_IDS = {
    "pico": 0xE48BFF56,
    "pico2": 0xE48BFF59,
}


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
                if report_id in reports:
                    valid = False
                    break
                reports[report_id] = (report_count, report_size, tag)
        if valid and cursor == end and data[end - 1] == 0xC0:
            candidates.append(reports)
    return candidates


def _objcopy_binary(source: Path, input_format: str | None, objcopy: str) -> bytes:
    with tempfile.TemporaryDirectory() as temporary_directory:
        output = Path(temporary_directory) / "artifact.bin"
        command = [objcopy]
        if input_format is not None:
            command.extend(("-I", input_format))
        command.extend(("-O", "binary", str(source), str(output)))
        try:
            subprocess.run(command, check=True, capture_output=True, text=True)
        except FileNotFoundError as error:
            raise ValueError(f"objcopy executable not found: {objcopy}") from error
        except subprocess.CalledProcessError as error:
            detail = error.stderr.strip() or error.stdout.strip() or str(error)
            raise ValueError(f"objcopy failed for {source}: {detail}") from error
        return output.read_bytes()


def _elf_load_base(elf: bytes) -> int:
    if len(elf) < 52 or elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] not in (1, 2):
        raise ValueError("pico_uart.elf is not a supported ELF32 file")
    byte_order = "<" if elf[5] == 1 else ">"
    program_header_offset = struct.unpack_from(f"{byte_order}I", elf, 28)[0]
    program_header_size, program_header_count = struct.unpack_from(f"{byte_order}HH", elf, 42)
    if program_header_size < 32 or program_header_offset + program_header_size * program_header_count > len(elf):
        raise ValueError("pico_uart.elf has an invalid program-header table")

    load_addresses = []
    for index in range(program_header_count):
        offset = program_header_offset + index * program_header_size
        segment_type, _file_offset, _virtual_address, physical_address, file_size = struct.unpack_from(
            f"{byte_order}IIIII", elf, offset
        )
        if segment_type == 1 and file_size != 0:
            load_addresses.append(physical_address)
    if not load_addresses:
        raise ValueError("pico_uart.elf contains no file-backed load segment")
    return min(load_addresses)


def _intel_hex_load_base(hex_data: bytes) -> int:
    upper_address = 0
    load_addresses = []
    saw_eof = False
    try:
        lines = hex_data.decode("ascii").splitlines()
    except UnicodeDecodeError as error:
        raise ValueError("pico_uart.hex is not ASCII Intel HEX") from error

    for line in lines:
        if not line.startswith(":"):
            raise ValueError("pico_uart.hex has an invalid record prefix")
        try:
            record = bytes.fromhex(line[1:])
        except ValueError as error:
            raise ValueError("pico_uart.hex contains non-hexadecimal data") from error
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 0xFF:
            raise ValueError("pico_uart.hex has an invalid record length or checksum")
        address = int.from_bytes(record[1:3], "big")
        record_type = record[3]
        payload = record[4:-1]
        if record_type == 0:
            if payload:
                load_addresses.append(upper_address + address)
        elif record_type == 1:
            saw_eof = True
        elif record_type == 2 and len(payload) == 2:
            upper_address = int.from_bytes(payload, "big") << 4
        elif record_type == 4 and len(payload) == 2:
            upper_address = int.from_bytes(payload, "big") << 16
        elif record_type not in (3, 5):
            raise ValueError(f"pico_uart.hex has an invalid record type {record_type}")

    if not saw_eof or not load_addresses:
        raise ValueError("pico_uart.hex is missing data or its EOF record")
    return min(load_addresses)


def _verify_uf2_payload(
    uf2: bytes, binary: bytes, expected_family_id: int, expected_base_address: int = PICO_FLASH_BASE
) -> None:
    if not uf2 or len(uf2) % 512 != 0:
        raise ValueError("pico_uart.uf2 does not contain complete 512-byte blocks")

    blocks_by_family = {}
    declared_counts = {}
    absolute_blocks = []
    covered = bytearray(len(binary))
    for offset in range(0, len(uf2), 512):
        block = uf2[offset : offset + 512]
        magic0, magic1, flags, target, payload_size, block_number, block_count, family_id = (
            struct.unpack_from("<IIIIIIII", block)
        )
        if magic0 != UF2_MAGIC_START0 or magic1 != UF2_MAGIC_START1 or struct.unpack_from("<I", block, 508)[0] != UF2_MAGIC_END:
            raise ValueError("pico_uart.uf2 has an invalid block marker")
        if payload_size == 0 or payload_size > 476:
            raise ValueError("pico_uart.uf2 has an invalid payload size")
        if (flags & UF2_FLAG_FAMILY_ID_PRESENT) == 0:
            raise ValueError("pico_uart.uf2 block is missing its family ID")
        allowed_families = {expected_family_id}
        if expected_family_id == UF2_FAMILY_IDS["pico2"]:
            allowed_families.add(UF2_FAMILY_RP2350_ABSOLUTE)
        if family_id not in allowed_families:
            raise ValueError(f"pico_uart.uf2 has unexpected family ID 0x{family_id:08x}")

        if family_id == UF2_FAMILY_RP2350_ABSOLUTE:
            absolute_blocks.append((flags, target, payload_size, block_number, block_count, block))
            continue

        family_blocks = blocks_by_family.setdefault(family_id, {})
        declared_count = declared_counts.setdefault(family_id, block_count)
        if block_count != declared_count or block_number >= block_count or block_number in family_blocks:
            raise ValueError("pico_uart.uf2 has inconsistent or duplicate block numbering")
        family_blocks[block_number] = (flags, target, block[32 : 32 + payload_size])

    for family_id, family_blocks in blocks_by_family.items():
        declared_count = declared_counts[family_id]
        if declared_count != len(family_blocks) or set(family_blocks) != set(range(declared_count)):
            raise ValueError("pico_uart.uf2 is missing one or more declared blocks")

    if expected_family_id not in blocks_by_family:
        raise ValueError("pico_uart.uf2 contains no firmware-family blocks")
    expects_absolute_block = expected_family_id == UF2_FAMILY_IDS["pico2"]
    if expects_absolute_block != bool(absolute_blocks):
        raise ValueError("pico_uart.uf2 has an invalid RP2350 absolute-block set")
    if absolute_blocks:
        flags, target, payload_size, block_number, block_count, block = absolute_blocks[0]
        if (
            len(absolute_blocks) != 1
            or flags != (UF2_FLAG_FAMILY_ID_PRESENT | UF2_FLAG_EXTENSION_FLAGS_PRESENT)
            or payload_size != 256
            or block_number != 0
            or block_count != 2
            or target != RP2350_ABSOLUTE_BLOCK_ADDRESS
            or block[32:288] != b"\xef" * 256
            or struct.unpack_from("<I", block, 288)[0] != UF2_EXTENSION_RP2_IGNORE_BLOCK
        ):
            raise ValueError("pico_uart.uf2 has a malformed RP2350 absolute block")

    flash_blocks = [
        entry
        for entry in blocks_by_family[expected_family_id].values()
        if (entry[0] & UF2_FLAG_NOT_MAIN_FLASH) == 0
    ]
    if not flash_blocks:
        raise ValueError("pico_uart.uf2 contains no main-flash payload")
    base_address = min(target for _flags, target, _payload in flash_blocks)
    if base_address != expected_base_address:
        raise ValueError(f"pico_uart.uf2 starts at unexpected address 0x{base_address:08x}")

    for _flags, target, payload in flash_blocks:
        binary_offset = target - base_address
        if binary_offset < 0 or binary_offset >= len(binary):
            raise ValueError("pico_uart.uf2 payload lies outside pico_uart.bin")
        compared_length = min(len(payload), len(binary) - binary_offset)
        if payload[:compared_length] != binary[binary_offset : binary_offset + compared_length]:
            raise ValueError("pico_uart.uf2 payload differs from pico_uart.bin")
        if any(payload[compared_length:]):
            raise ValueError("pico_uart.uf2 has nonzero payload beyond pico_uart.bin")
        if any(covered[binary_offset : binary_offset + compared_length]):
            raise ValueError("pico_uart.uf2 contains overlapping main-flash payloads")
        covered[binary_offset : binary_offset + compared_length] = b"\x01" * compared_length

    if not all(covered):
        raise ValueError("pico_uart.uf2 does not cover the complete pico_uart.bin payload")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--board", required=True, choices=sorted(UF2_FAMILY_IDS))
    parser.add_argument("--version", default="0.0.0-dev")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--objcopy", default="arm-none-eabi-objcopy")
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
    if not artifacts["hex"].read_bytes().startswith(b":"):
        raise SystemExit("pico_uart.hex is not Intel HEX")

    data = artifacts["bin"].read_bytes()
    if not data:
        raise SystemExit("pico_uart.bin is empty")
    try:
        if _elf_load_base(elf) != PICO_FLASH_BASE:
            raise ValueError("pico_uart.elf does not start at the Pico flash base")
        if _intel_hex_load_base(artifacts["hex"].read_bytes()) != PICO_FLASH_BASE:
            raise ValueError("pico_uart.hex does not start at the Pico flash base")
        if _objcopy_binary(artifacts["elf"], None, args.objcopy) != data:
            raise ValueError("pico_uart.elf load image differs from pico_uart.bin")
        if _objcopy_binary(artifacts["hex"], "ihex", args.objcopy) != data:
            raise ValueError("pico_uart.hex payload differs from pico_uart.bin")
        _verify_uf2_payload(artifacts["uf2"].read_bytes(), data, UF2_FAMILY_IDS[args.board])
    except ValueError as error:
        raise SystemExit(str(error)) from error
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

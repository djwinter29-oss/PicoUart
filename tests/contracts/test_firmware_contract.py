"""Contract tests keeping host constants synchronized with firmware headers."""

from __future__ import annotations

import re
import struct

import pytest
from tomlkit import parse

from contract import (
    firmware_cdc_interface_strings,
    firmware_hid_constants,
    firmware_hid_interface_number,
    firmware_hid_report_count,
    firmware_hid_reset_cmake_default_enabled,
    firmware_hid_reset_default_enabled,
    firmware_hid_status_report_count,
    firmware_uart_board_ports,
    firmware_usb_ids,
    firmware_usb_product_string,
    is_lab_placeholder_identity,
)
import pico_uart as hid
from pico_uart import protocol


def test_release_lock_matches_direct_requirement_pins(repo_root):
    requirement_dir = repo_root / "host" / "python"
    pin_pattern = re.compile(r"^([A-Za-z0-9_.-]+)==([^\s;\\]+)", re.MULTILINE)
    project = parse((requirement_dir / "pyproject.toml").read_text())
    requirements = project["project"]["dependencies"] + project["dependency-groups"]["test"]
    requirements += [requirement for requirement in project["dependency-groups"]["dev"] if isinstance(requirement, str)]
    requirements += project["build-system"]["requires"]
    direct_pins = dict(pin_pattern.findall("\n".join(requirements)))
    locked_pins = dict(pin_pattern.findall((requirement_dir / "requirements-lock.txt").read_text()))
    assert direct_pins.items() <= locked_pins.items()
    assert not (requirement_dir / "requirements.txt").exists()
    assert not (requirement_dir / "requirements-dev.txt").exists()


def test_runtime_lock_matches_runtime_pins_without_test_dependencies(repo_root):
    requirement_dir = repo_root / "host/python"
    pin_pattern = re.compile(r"^([A-Za-z0-9_.-]+)==([^\s;\\]+)", re.MULTILINE)
    project = parse((requirement_dir / "pyproject.toml").read_text())
    runtime_pins = dict(pin_pattern.findall("\n".join(project["project"]["dependencies"])))
    locked_pins = dict(pin_pattern.findall((requirement_dir / "requirements-runtime-lock.txt").read_text()))
    development_pins = dict(pin_pattern.findall((requirement_dir / "requirements-lock.txt").read_text()))
    assert runtime_pins.items() <= locked_pins.items()
    assert locked_pins.items() <= development_pins.items()
    assert not {"pytest", "pytest-cov", "coverage", "pluggy", "iniconfig"}.intersection(locked_pins)


def test_usb_ids_parse_from_firmware_defines(repo_root):
    vid, pid = firmware_usb_ids(repo_root)
    assert vid == hid.VENDOR_ID
    assert pid == hid.PRODUCT_ID


def test_hid_interface_number_matches_descriptor_enum(repo_root):
    assert firmware_hid_interface_number(repo_root) == hid.HID_INTERFACE_NUMBER


def test_hid_layout_and_command_constants_match_firmware(repo_root):
    fw = firmware_hid_constants(repo_root)
    assert fw["USB_HID_REPORT_VERSION"] == hid.STATUS_LAYOUT_VERSION
    assert fw["USB_HID_REPORT_VERSION"] == hid.BOARD_STATUS_LAYOUT_VERSION
    assert fw["USB_HID_REPORT_ID_STATUS"] == hid.REPORT_ID_STATUS
    assert fw["USB_HID_REPORT_ID_BOARD_STATUS"] == hid.REPORT_ID_BOARD_STATUS
    assert fw["USB_HID_REPORT_ID_COMMAND"] == hid.REPORT_ID_COMMAND
    assert fw["USB_HID_COMMAND_TOGGLE_LED"] == hid.COMMAND_TOGGLE_LED
    assert fw["USB_HID_COMMAND_RESET_BOARD"] == hid.COMMAND_RESET_BOARD
    assert fw["USB_HID_COMMAND_ARM_RESET"] == hid.COMMAND_ARM_RESET
    assert fw["USB_HID_RESET_ARM_WINDOW_MS"] == int(hid.RESET_ARM_WINDOW_S * 1000)
    assert fw["USB_HID_BOARD_STATUS_FLAG_HID_RESET"] == hid.BOARD_STATUS_FLAG_HID_RESET
    assert hid.BOARD_STATUS_RESERVED0_KNOWN_FLAGS == hid.BOARD_STATUS_FLAG_HID_RESET
    assert fw["USB_HID_SIGNATURE0"] == hid.STATUS_SIGNATURE
    assert hid.STATUS_SIZE == 63
    assert hid.STATUS_SIZE == hid.STATUS_HEADER_SIZE + (hid.UART_CHANNEL_COUNT * hid.STATUS_CHANNEL_SIZE)
    assert hid.BOARD_STATUS_SIZE == 8
    assert hid.STATUS_SIZE + 1 <= 64


def test_hid_descriptor_status_report_count_matches_host_payload(repo_root):
    assert firmware_hid_status_report_count(repo_root) == hid.STATUS_SIZE


def test_hid_descriptor_board_status_report_count_matches_host_payload(repo_root):
    assert firmware_hid_report_count(repo_root, hid.REPORT_ID_BOARD_STATUS) == hid.BOARD_STATUS_SIZE


def test_hid_descriptor_command_report_count_matches_host_payload(repo_root):
    assert firmware_hid_report_count(repo_root, hid.REPORT_ID_COMMAND) == 1


def test_hid_telemetry_report_contract(repo_root):
    fw = firmware_hid_constants(repo_root)
    assert fw["USB_HID_REPORT_ID_TELEMETRY"] == protocol.REPORT_ID_TELEMETRY == 18
    assert fw["USB_HID_TELEMETRY_VERSION"] == protocol.TELEMETRY_LAYOUT_VERSION == 1
    assert firmware_hid_report_count(repo_root, protocol.REPORT_ID_TELEMETRY) == protocol.TELEMETRY_SIZE == 59
    # Control-transfer payloads are limited to one 64-byte endpoint buffer including the report ID.
    assert protocol.TELEMETRY_SIZE + 1 <= 64


@pytest.mark.parametrize("retired_report_id", [5, 8, 9])
def test_retired_reports_are_absent_from_the_descriptor(repo_root, retired_report_id):
    with pytest.raises(ValueError):
        firmware_hid_report_count(repo_root, retired_report_id)


def test_telemetry_field_offsets_match_between_firmware_and_host(repo_root):
    source = (repo_root / "firmware" / "src" / "usb" / "usb_hid.c").read_text(encoding="utf-8")
    firmware_offsets = {
        name: int(offset) for name, offset in re.findall(
            r"offsetof\(usb_hid_telemetry_report_t, (\w+)\) == (\d+)u", source)
    }
    assert firmware_offsets.keys() == {
        "uptime_ms", "service_load_basis_points", "ram_reserved_bytes", "rx_overflow_count",
        "config_save_status", "diagnostics_generation",
    }
    # Field order of the host decoder: version, led, temperature, uptime, load x2, RAM x2, overflow x6, save x2, gens x6.
    host_offsets = {
        "uptime_ms": struct.calcsize("<BBh"),
        "service_load_basis_points": struct.calcsize("<BBhQ"),
        "ram_reserved_bytes": struct.calcsize("<BBhQHH"),
        "rx_overflow_count": struct.calcsize("<BBhQHHII"),
        "config_save_status": struct.calcsize("<BBhQHHII6I"),
        "diagnostics_generation": struct.calcsize("<BBhQHHII6IBI"),
    }
    assert firmware_offsets == host_offsets


def test_persistent_configuration_report_contracts(repo_root):
    constants = firmware_hid_constants(repo_root)
    assert constants["USB_HID_REPORT_ID_CONFIGURATION"] == protocol.REPORT_ID_CONFIGURATION == 16
    assert constants["USB_HID_REPORT_ID_CONFIGURATION_SAVE"] == protocol.REPORT_ID_CONFIGURATION_SAVE == 17
    assert firmware_hid_report_count(repo_root, 16) == protocol.CONFIGURATION_SIZE == 12
    assert firmware_hid_report_count(repo_root, 17) == protocol.CONFIGURATION_SAVE_SIZE == 8


def test_channel_diagnostic_reports_match_current_layout(repo_root):
    assert firmware_hid_constants(repo_root)["USB_HID_REPORT_ID_CHANNEL_BASE"] == protocol.REPORT_ID_CHANNEL_BASE
    assert protocol.CHANNEL_DIAGNOSTICS_SIZE == 59
    for report_id in range(protocol.REPORT_ID_CHANNEL_BASE, protocol.REPORT_ID_CHANNEL_BASE + 6):
        assert firmware_hid_report_count(repo_root, report_id) == protocol.CHANNEL_DIAGNOSTICS_SIZE


def test_hardware_info_contract_matches_current_layout(repo_root):
    fw = firmware_hid_constants(repo_root)
    assert fw["USB_HID_REPORT_ID_HARDWARE_INFO"] == protocol.REPORT_ID_HARDWARE_INFO == 6
    assert fw["USB_HID_HARDWARE_INFO_VERSION"] == protocol.HARDWARE_INFO_LAYOUT_VERSION == 1
    assert fw["USB_HID_MCU_RP2040"] == protocol.MCU_RP2040
    assert fw["USB_HID_MCU_RP2350"] == protocol.MCU_RP2350
    assert firmware_hid_report_count(repo_root, protocol.REPORT_ID_HARDWARE_INFO) == protocol.HARDWARE_INFO_SIZE == 6
    assert fw["USB_HID_REPORT_VERSION"] == 1


def test_lab_placeholder_helper_is_independent_of_tree_identity():
    assert is_lab_placeholder_identity(0xCAFE, 0x4010) is True
    assert is_lab_placeholder_identity(0x1209, 0x0001) is False


def test_usb_identity_checker_accepts_tree_identity(repo_root):
    import subprocess
    import sys

    script = repo_root / "tools" / "release" / "check-usb-identity.py"
    result = subprocess.run(
        [sys.executable, str(script), "--repo-root", str(repo_root)],
        cwd=repo_root,
        check=False,
    )
    assert result.returncode == 0


def test_usb_product_string_uses_short_product_name(repo_root):
    product = firmware_usb_product_string(repo_root)
    assert product == hid.PRODUCT_STRING == "PicoUart"
    assert len(product) <= 32


def test_cdc_interface_strings_advertise_hw_and_pio_8n1(repo_root):
    assert firmware_cdc_interface_strings(repo_root) == [
        "CDC0 HW",
        "CDC1 HW",
        "CDC2 PIO 8N1",
        "CDC3 PIO 8N1",
        "CDC4 PIO 8N1",
        "CDC5 PIO 8N1",
    ]


def test_hid_reset_enabled_by_default(repo_root):
    assert firmware_hid_reset_default_enabled(repo_root) is True
    assert firmware_hid_reset_cmake_default_enabled(repo_root) is True


def test_uart_board_map_matches_documented_tx_rx_and_hw_fc_default(repo_root):
    ports = firmware_uart_board_ports(repo_root)
    expected = [
        ("UART_PORT_0", "UART_DRIVER_BACKEND_HW", 0, 1, 2, 3, False),
        ("UART_PORT_1", "UART_DRIVER_BACKEND_HW", 4, 5, 6, 7, False),
        ("UART_PORT_2", "UART_DRIVER_BACKEND_PIO", 8, 9, None, None, None),
        ("UART_PORT_3", "UART_DRIVER_BACKEND_PIO", 12, 13, None, None, None),
        ("UART_PORT_4", "UART_DRIVER_BACKEND_PIO", 16, 17, None, None, None),
        ("UART_PORT_5", "UART_DRIVER_BACKEND_PIO", 20, 21, None, None, None),
    ]
    assert len(ports) == len(expected)
    for port, want in zip(ports, expected, strict=True):
        assert port["id"] == want[0]
        assert port["backend"] == want[1]
        assert port["tx_pin"] == want[2]
        assert port["rx_pin"] == want[3]
        if want[1] == "UART_DRIVER_BACKEND_HW":
            assert port["cts_pin"] == want[4]
            assert port["rts_pin"] == want[5]
            assert port["hardware_flow_control"] is want[6]

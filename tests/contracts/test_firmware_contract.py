"""Contract tests keeping host constants synchronized with firmware headers."""

from __future__ import annotations

import re
import pytest
from tomlkit import parse

from contract import (
    firmware_cdc_interface_strings,
    firmware_hid_constants,
    firmware_hid_interface_number,
    firmware_hid_report_count,
    firmware_hid_reset_cmake_default_enabled,
    firmware_hid_reset_default_enabled,
    firmware_uart_board_ports,
    firmware_usb_ids,
    firmware_usb_product_string,
    is_lab_placeholder_identity,
)
import pico_uart as hid


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
    assert fw["USB_HID_REPORT_VERSION"] == 1
    assert fw["USB_HID_REPORT_ID_DEVICE_INFO"] == 1
    assert fw["USB_HID_REPORT_ID_CONFIGURATION"] == 2
    assert fw["USB_HID_REPORT_ID_CONTROL"] == 20
    assert fw["USB_HID_REPORT_ID_TELEMETRY"] == 3
    assert fw["USB_HID_REPORT_ID_CHANNEL_BASE"] == 10
    assert fw["USB_HID_REPORT_ID_DIAGNOSTIC_BASE"] == 30
    assert fw["USB_HID_DEVICE_INFO_SIZE"] == 27
    assert fw["USB_HID_CONFIGURATION_SIZE"] == 36
    assert fw["USB_HID_CONTROL_SIZE"] == 4
    assert fw["USB_HID_TELEMETRY_SIZE"] == 20
    assert fw["USB_HID_CHANNEL_SIZE"] == 30
    assert fw["USB_HID_DIAGNOSTIC_SIZE"] == 24
    assert fw["USB_HID_CHANNEL_INTERVAL_MS"] == 500
    assert fw["USB_HID_COMMAND_LED_OFF"] == 1
    assert fw["USB_HID_COMMAND_LED_ON"] == 2
    assert fw["USB_HID_COMMAND_REBOOT"] == 3
    assert fw["USB_HID_COMMAND_SAVE_CONFIGURATION"] == 4
    assert all(size + 1 <= 64 for size in (27, 36, 20, 30, 4, 24))


def test_hid_descriptor_status_report_count_matches_host_payload(repo_root):
    assert firmware_hid_report_count(repo_root, 1) == 27


def test_hid_descriptor_board_status_report_count_matches_host_payload(repo_root):
    assert firmware_hid_report_count(repo_root, 2) == 36
    assert firmware_hid_report_count(repo_root, 3) == 20


def test_hid_descriptor_command_report_count_matches_host_payload(repo_root):
    assert firmware_hid_report_count(repo_root, 20) == 4
    assert firmware_hid_report_count(repo_root, 10) == 30
    assert firmware_hid_report_count(repo_root, 15) == 30
    assert firmware_hid_report_count(repo_root, 30) == 24
    assert firmware_hid_report_count(repo_root, 35) == 24


def test_legacy_hid_reports_are_absent(repo_root):
    fw = firmware_hid_constants(repo_root)
    assert fw["USB_HID_REPORT_ID_TELEMETRY"] == 3
    assert fw["USB_HID_REPORT_ID_CHANNEL_BASE"] == 10
    assert firmware_hid_report_count(repo_root, 1) == 27
    assert firmware_hid_report_count(repo_root, 2) == 36
    assert firmware_hid_report_count(repo_root, 3) == 20
    assert [firmware_hid_report_count(repo_root, report_id) for report_id in range(10, 16)] == [30] * 6


@pytest.mark.parametrize("retired_report_id", [*range(4, 10), *range(16, 20), *range(21, 30), *range(36, 256)])
def test_retired_reports_are_absent_from_the_descriptor(repo_root, retired_report_id):
    with pytest.raises(ValueError):
        firmware_hid_report_count(repo_root, retired_report_id)


def test_channel_reports_have_input_and_feature_contract_in_source(repo_root):
    descriptor = (repo_root / "firmware/src/usb/usb_descriptors.c").read_text(encoding="utf-8")
    for report_id in range(10, 16):
        start = descriptor.index(f"0x85, 0x{report_id:02X}")
        end = descriptor.find("0x85,", start + 1)
        chunk = descriptor[start:] if end < 0 else descriptor[start:end]
        assert chunk.count("0x81, 0x02") == 1
        assert chunk.count("0xB1, 0x02") == 1


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

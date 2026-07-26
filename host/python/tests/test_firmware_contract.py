"""Contract tests keeping host constants synchronized with firmware headers."""

from __future__ import annotations

from contract import (
    firmware_hid_constants,
    firmware_hid_status_report_count,
    firmware_usb_ids,
    is_lab_placeholder_identity,
)


def test_usb_ids_parse_from_firmware_defines(hid_module, repo_root):
    vid, pid = firmware_usb_ids(repo_root)
    assert vid == hid_module.VENDOR_ID
    assert pid == hid_module.PRODUCT_ID


def test_hid_layout_and_command_constants_match_firmware(hid_module, repo_root):
    fw = firmware_hid_constants(repo_root)
    assert fw["USB_HID_REPORT_VERSION"] == hid_module.STATUS_LAYOUT_VERSION
    assert fw["USB_HID_REPORT_VERSION"] == hid_module.BOARD_STATUS_LAYOUT_VERSION
    assert fw["USB_HID_REPORT_ID_STATUS"] == hid_module.REPORT_ID_STATUS
    assert fw["USB_HID_REPORT_ID_BOARD_STATUS"] == hid_module.REPORT_ID_BOARD_STATUS
    assert fw["USB_HID_REPORT_ID_COMMAND"] == hid_module.REPORT_ID_COMMAND
    assert fw["USB_HID_COMMAND_TOGGLE_LED"] == hid_module.COMMAND_TOGGLE_LED
    assert fw["USB_HID_COMMAND_RESET_BOARD"] == hid_module.COMMAND_RESET_BOARD
    assert fw["USB_HID_COMMAND_ARM_RESET"] == hid_module.COMMAND_ARM_RESET
    assert fw["USB_HID_RESET_ARM_WINDOW_MS"] == int(hid_module.RESET_ARM_WINDOW_S * 1000)
    assert hid_module.STATUS_SIZE == 63
    assert hid_module.BOARD_STATUS_SIZE == 8
    assert hid_module.STATUS_SIZE + 1 <= 64


def test_hid_descriptor_status_report_count_matches_host_payload(hid_module, repo_root):
    assert firmware_hid_status_report_count(repo_root) == hid_module.STATUS_SIZE


def test_lab_placeholder_helper_and_current_tree_identity(repo_root):
    assert is_lab_placeholder_identity(0xCAFE, 0x4010) is True
    assert is_lab_placeholder_identity(0x1209, 0x0001) is False
    # Development tree ships the lab placeholder until a release identity is allocated.
    vid, pid = firmware_usb_ids(repo_root)
    assert is_lab_placeholder_identity(vid, pid) is True

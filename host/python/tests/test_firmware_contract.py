"""Contract tests keeping host constants synchronized with firmware headers."""

from __future__ import annotations

from contract import firmware_hid_report_version, firmware_usb_ids


def test_usb_ids_parse_from_firmware_defines(hid_module, repo_root):
    vid, pid = firmware_usb_ids(repo_root)
    assert vid == hid_module.VENDOR_ID
    assert pid == hid_module.PRODUCT_ID


def test_hid_layout_version_matches_firmware(hid_module, repo_root):
    version = firmware_hid_report_version(repo_root)
    assert version == hid_module.STATUS_LAYOUT_VERSION
    assert version == hid_module.BOARD_STATUS_LAYOUT_VERSION

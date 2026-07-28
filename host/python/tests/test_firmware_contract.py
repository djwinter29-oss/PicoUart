"""Contract tests keeping host constants synchronized with firmware headers."""

from __future__ import annotations

from contract import (
    firmware_hid_constants,
    firmware_hid_report_count,
    firmware_hid_reset_default_enabled,
    firmware_hid_status_report_count,
    firmware_uart_board_ports,
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
    assert fw["USB_HID_SIGNATURE0"] == ord("P")
    assert hid_module.STATUS_SIZE == 63
    assert hid_module.STATUS_SIZE == 3 + (6 * 10)
    assert hid_module.BOARD_STATUS_SIZE == 8
    assert hid_module.STATUS_SIZE + 1 <= 64


def test_hid_descriptor_status_report_count_matches_host_payload(hid_module, repo_root):
    assert firmware_hid_status_report_count(repo_root) == hid_module.STATUS_SIZE


def test_hid_descriptor_board_status_report_count_matches_host_payload(hid_module, repo_root):
    assert firmware_hid_report_count(repo_root, 3) == hid_module.BOARD_STATUS_SIZE


def test_hid_descriptor_command_report_count_matches_host_payload(hid_module, repo_root):
    assert firmware_hid_report_count(repo_root, 4) == 1


def test_lab_placeholder_helper_and_current_tree_identity(repo_root):
    assert is_lab_placeholder_identity(0xCAFE, 0x4010) is True
    assert is_lab_placeholder_identity(0x1209, 0x0001) is False
    # Development tree ships the lab placeholder until a release identity is allocated.
    vid, pid = firmware_usb_ids(repo_root)
    assert is_lab_placeholder_identity(vid, pid) is True


def test_hid_reset_disabled_by_default(repo_root):
    assert firmware_hid_reset_default_enabled(repo_root) is False


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

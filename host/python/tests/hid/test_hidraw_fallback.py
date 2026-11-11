# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Hardware-free tests for the Linux hidraw fallback using a pipe, a fake fcntl, and a fake sysfs tree."""

import os

import pytest

from pico_uart.hid import transport

pytestmark = pytest.mark.skipif(os.name != "posix", reason="hidraw fallback is POSIX-only")


def test_feature_ioctl_numbers_match_linux_hidraw_header():
    assert transport._hidraw_feature_ioctl(0x07, 8) == 0xC0084807  # HIDIOCGFEATURE(8)
    assert transport._hidraw_feature_ioctl(0x06, 5) == 0xC0054806  # HIDIOCSFEATURE(5)


def test_resolve_hidraw_path_matches_interface_directory_name(tmp_path, monkeypatch):
    interface = tmp_path / "devices/usb1/1-1/1-1:1.2"
    interface.mkdir(parents=True)
    (tmp_path / "class/hidraw/hidraw7").mkdir(parents=True)
    (tmp_path / "class/hidraw/hidraw7/device").symlink_to(interface)
    monkeypatch.setattr(transport, "Path", lambda _: tmp_path / "class/hidraw")

    assert transport._resolve_hidraw_path("1-1:1.2") == "/dev/hidraw7"
    assert transport._resolve_hidraw_path("1-1:1.9") is None
    assert transport._resolve_hidraw_path("/dev/hidraw7") is None
    assert transport._resolve_hidraw_path("") is None


def test_hidraw_device_reads_with_timeout_and_closes():
    read_end, write_end = os.pipe()
    try:
        device = transport._LinuxHidrawDevice.__new__(transport._LinuxHidrawDevice)
        device._file_descriptor = read_end
        assert device.read(8, 1) == []
        os.write(write_end, bytes((1, 2, 3)))
        assert device.read(8, 100) == [1, 2, 3]
    finally:
        os.close(read_end)
        os.close(write_end)


def test_hidraw_feature_reports_use_ioctl_with_report_id_prefix(monkeypatch):
    calls = []

    class FakeFcntl:
        @staticmethod
        def ioctl(descriptor, request, buffer, mutate):
            calls.append((request, bytes(buffer), mutate))
            if request == transport._hidraw_feature_ioctl(0x07, 4):
                buffer[1:3] = b"\xaa\xbb"
                return 3
            return len(buffer)

    monkeypatch.setattr(transport, "fcntl", FakeFcntl)
    device = transport._LinuxHidrawDevice.__new__(transport._LinuxHidrawDevice)
    device._file_descriptor = -1

    assert device.get_feature_report(9, 4) == [9, 0xAA, 0xBB]
    assert calls[0][1][0] == 9
    assert device.send_feature_report([20, 1, 4]) == 3


def test_hidraw_feature_reports_fail_clearly_without_fcntl(monkeypatch):
    monkeypatch.setattr(transport, "fcntl", None)
    device = transport._LinuxHidrawDevice.__new__(transport._LinuxHidrawDevice)
    with pytest.raises(OSError, match="requires fcntl"):
        device.get_feature_report(1, 4)
    with pytest.raises(OSError, match="requires fcntl"):
        device.send_feature_report([1])


def test_open_enumerated_device_prefers_resolved_hidraw_node(monkeypatch):
    opened = []
    monkeypatch.setattr(transport, "require_hid", lambda: None)
    monkeypatch.setattr(transport, "_resolve_hidraw_path", lambda path: "/dev/hidraw3")
    monkeypatch.setattr(transport, "_open_hidraw_device", lambda path: opened.append(path) or "device")

    assert transport.open_enumerated_device({"path": b"1-1:1.2"}) == "device"
    assert opened == ["/dev/hidraw3"]


def test_open_enumerated_device_reports_permission_hint_on_os_error(monkeypatch):
    def deny(path):
        raise PermissionError("denied")

    monkeypatch.setattr(transport, "require_hid", lambda: None)
    monkeypatch.setattr(transport, "_resolve_hidraw_path", lambda path: "/dev/hidraw3")
    monkeypatch.setattr(transport, "_open_hidraw_device", deny)

    with pytest.raises(RuntimeError, match="check hidraw permissions"):
        transport.open_enumerated_device({"path": "1-1:1.2"})


def test_open_enumerated_device_rejects_invalid_path(monkeypatch):
    monkeypatch.setattr(transport, "require_hid", lambda: None)
    with pytest.raises(RuntimeError, match="invalid device path"):
        transport.open_enumerated_device({"path": ""})


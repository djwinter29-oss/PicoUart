"""Mocked hidapi discovery tests for fail-closed collection selection."""

from __future__ import annotations

import pytest


def _install_hid_mock(monkeypatch, hid_module, devices):
    opened_paths = []

    class Device:
        def open_path(self, path):
            opened_paths.append(path)

    monkeypatch.setattr(
        hid_module.hid,
        "enumerate",
        lambda vendor_id, product_id: devices,
    )
    monkeypatch.setattr(hid_module.hid, "device", Device)
    return opened_paths


def _exact_device(hid_module, path=b"correct"):
    return {
        "path": path,
        "usage_page": hid_module.USAGE_PAGE,
        "usage": hid_module.USAGE,
    }


def test_discovery_opens_correct_collection_among_incorrect(monkeypatch, hid_module):
    devices = [
        {"path": b"incorrect", "usage_page": 1, "usage": 2},
        _exact_device(hid_module),
    ]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    hid_module.open_device()

    assert opened_paths == [b"correct"]


def test_discovery_resolves_linux_interface_path_to_hidraw(
    monkeypatch, hid_module
):
    devices = [_exact_device(hid_module, b"1-3:1.12")]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)
    monkeypatch.setattr(hid_module, "_resolve_hidraw_path", lambda path: "/dev/hidraw7")

    hid_module.open_device()

    assert opened_paths == [b"1-3:1.12"]


def test_discovery_uses_hidraw_adapter_when_hidapi_open_fails(
    monkeypatch, hid_module
):
    devices = [_exact_device(hid_module, b"1-3:1.12")]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)
    fallback_device = object()
    monkeypatch.setattr(hid_module, "_resolve_hidraw_path", lambda path: "/dev/hidraw7")
    monkeypatch.setattr(hid_module, "_open_hidraw_device", lambda path: fallback_device)

    class FailingDevice:
        def open_path(self, path):
            opened_paths.append(path)
            raise OSError("libusb open failed")

    monkeypatch.setattr(hid_module.hid, "device", FailingDevice)

    assert hid_module.open_device() is fallback_device
    assert opened_paths == [b"1-3:1.12", b"/dev/hidraw7"]


def test_discovery_reports_original_path_when_hidraw_resolution_fails(
    monkeypatch, hid_module
):
    devices = [_exact_device(hid_module, b"1-3:1.12")]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)
    monkeypatch.setattr(hid_module, "_resolve_hidraw_path", lambda path: None)

    class FailingDevice:
        def open_path(self, path):
            opened_paths.append(path)
            raise OSError("libusb open failed")

    monkeypatch.setattr(hid_module.hid, "device", FailingDevice)

    with pytest.raises(RuntimeError, match="1-3:1.12"):
        hid_module.open_device()


def test_discovery_rejects_only_incorrect_collection(monkeypatch, hid_module):
    opened_paths = _install_hid_mock(
        monkeypatch,
        hid_module,
        [{"path": b"incorrect", "usage_page": 1, "usage": 2}],
    )

    with pytest.raises(RuntimeError, match="expected usage metadata"):
        hid_module.open_device()
    assert opened_paths == []


def test_discovery_allows_unique_validated_missing_usage_fallback(
    monkeypatch, hid_module
):
    devices = [{
        "path": b"fallback",
        "product_string": hid_module.PRODUCT_STRING,
        "interface_number": hid_module.HID_INTERFACE_NUMBER,
    }]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    hid_module.open_device()

    assert opened_paths == [b"fallback"]


def test_discovery_allows_empty_product_on_expected_interface(
    monkeypatch, hid_module
):
    devices = [{
        "path": b"linux-hidraw",
        "product_string": "",
        "interface_number": hid_module.HID_INTERFACE_NUMBER,
    }]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    hid_module.open_device()

    assert opened_paths == [b"linux-hidraw"]


def test_discovery_rejects_empty_product_with_unknown_interface(
    monkeypatch, hid_module
):
    devices = [{
        "path": b"ambiguous",
        "product_string": "",
        "interface_number": None,
    }]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    with pytest.raises(RuntimeError, match="expected usage metadata"):
        hid_module.open_device()
    assert opened_paths == []


@pytest.mark.parametrize("interface_number", [None, -1])
def test_discovery_allows_unknown_interface_metadata(
    monkeypatch, hid_module, interface_number
):
    devices = [{
        "path": b"fallback",
        "product_string": hid_module.PRODUCT_STRING,
        "interface_number": interface_number,
    }]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    hid_module.open_device()

    assert opened_paths == [b"fallback"]


def test_discovery_rejects_missing_usage_with_wrong_interface(
    monkeypatch, hid_module
):
    devices = [{
        "path": b"wrong-interface",
        "product_string": hid_module.PRODUCT_STRING,
        "interface_number": hid_module.HID_INTERFACE_NUMBER - 1,
    }]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    with pytest.raises(RuntimeError, match="expected usage metadata"):
        hid_module.open_device()
    assert opened_paths == []


def test_discovery_rejects_multiple_exact_matches(monkeypatch, hid_module):
    opened_paths = _install_hid_mock(
        monkeypatch,
        hid_module,
        [_exact_device(hid_module, b"first"), _exact_device(hid_module, b"second")],
    )

    with pytest.raises(RuntimeError, match="multiple PicoUart HID interfaces"):
        hid_module.open_device()
    assert opened_paths == []


def test_discovery_selects_exact_match_by_serial(monkeypatch, hid_module):
    devices = [
        {**_exact_device(hid_module, b"first"), "serial_number": "first-serial"},
        {**_exact_device(hid_module, b"second"), "serial_number": "second-serial"},
    ]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    hid_module.open_device(serial_number="second-serial")

    assert opened_paths == [b"second"]


def test_discovery_rejects_unknown_serial(monkeypatch, hid_module):
    devices = [{**_exact_device(hid_module), "serial_number": "known-serial"}]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    with pytest.raises(RuntimeError, match="interface not found"):
        hid_module.open_device(serial_number="unknown-serial")
    assert opened_paths == []


def test_discovery_selects_exact_match_by_path(monkeypatch, hid_module):
    devices = [_exact_device(hid_module, b"first"), _exact_device(hid_module, b"second")]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    hid_module.open_device(device_path="second")

    assert opened_paths == [b"second"]


def test_discovery_rejects_multiple_missing_usage_fallbacks(monkeypatch, hid_module):
    devices = [
        {
            "path": path,
            "product_string": hid_module.PRODUCT_STRING,
            "interface_number": hid_module.HID_INTERFACE_NUMBER,
        }
        for path in (b"first", b"second")
    ]
    opened_paths = _install_hid_mock(monkeypatch, hid_module, devices)

    with pytest.raises(RuntimeError, match="multiple PicoUart HID interfaces"):
        hid_module.open_device()
    assert opened_paths == []

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

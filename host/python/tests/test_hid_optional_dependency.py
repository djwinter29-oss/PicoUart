"""Regression test: pico_uart_hid must import without the hidapi runtime.

Without this, a missing/unbuildable hidapi wheel (e.g. no prebuilt wheel for
the host Python, see docs/tests/self-test-setup.md) previously raised
SystemExit at *module import time*, which pytest's conftest.py imports
unconditionally -- failing collection for the entire host test suite, not
just the HID-dependent tests.
"""

from __future__ import annotations

import importlib
import sys

import pytest


def _reload_without_hid(monkeypatch):
    """Reimport pico_uart_hid with the ``hid`` package unimportable."""
    monkeypatch.setitem(sys.modules, "hid", None)  # forces ImportError on `import hid`
    monkeypatch.delitem(sys.modules, "pico_uart_hid", raising=False)
    module = importlib.import_module("pico_uart_hid")
    return module


def test_module_imports_without_hidapi_installed(monkeypatch):
    module = _reload_without_hid(monkeypatch)
    try:
        assert module.hid is None
        # Constants (used by contract tests) must remain usable without hidapi.
        assert module.VENDOR_ID == 0xCAFE
    finally:
        monkeypatch.delitem(sys.modules, "pico_uart_hid", raising=False)
        importlib.import_module("pico_uart_hid")


def test_open_device_raises_clear_runtime_error_without_hidapi(monkeypatch):
    module = _reload_without_hid(monkeypatch)
    try:
        with pytest.raises(RuntimeError, match="Missing dependency"):
            module.open_device()
    finally:
        monkeypatch.delitem(sys.modules, "pico_uart_hid", raising=False)
        importlib.import_module("pico_uart_hid")

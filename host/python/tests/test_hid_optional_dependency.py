"""Regression test: pico_uart_hid must import without the hidapi runtime.

Without this, a missing/unbuildable hidapi wheel (no prebuilt wheel for the
host Python, or a native-extension build failure in a sandbox) previously
raised SystemExit at *module import time*, which pytest's conftest.py imports
unconditionally -- failing collection for the entire host test suite, not
just the HID-dependent tests.
"""

from __future__ import annotations

import contextlib
import importlib
import sys
import types

import pytest


@contextlib.contextmanager
def _hid_unavailable():
    """Temporarily import without hidapi; restore exact modules even on failure."""
    with pytest.MonkeyPatch.context() as patch:
        patch.setitem(sys.modules, "hid", None)
        patch.delitem(sys.modules, "pico_uart_hid", raising=False)
        module = importlib.import_module("pico_uart_hid")
        patch.setitem(sys.modules, "pico_uart_hid", module)
        yield module


def test_module_imports_without_hidapi_installed():
    with _hid_unavailable() as module:
        assert module.hid is None
        # Constants (used by contract tests) must remain usable without hidapi.
        assert module.VENDOR_ID == 0xCAFE


def test_open_device_raises_clear_runtime_error_without_hidapi():
    with _hid_unavailable() as module:
        with pytest.raises(RuntimeError, match="Missing dependency"):
            module.open_device()


@pytest.mark.parametrize("body_fails", [False, True])
def test_hid_unavailable_helper_restores_real_hid_afterward(monkeypatch, body_fails):
    """The module must not stay cached with hid=None after ``_hid_unavailable``
    exits, even though sys.modules is briefly poisoned with ``hid: None``
    while inside it (regression for a finally-block reimport that used to run
    before the hid=None patch was undone).
    """
    fake_hid = types.ModuleType("hid")
    monkeypatch.setitem(sys.modules, "hid", fake_hid)
    monkeypatch.delitem(sys.modules, "pico_uart_hid", raising=False)
    baseline = importlib.import_module("pico_uart_hid")

    expected = pytest.raises(ValueError) if body_fails else contextlib.nullcontext()
    with expected:
        with _hid_unavailable() as module:
            assert module.hid is None
            if body_fails:
                raise ValueError("test body failed")

    assert sys.modules["hid"] is fake_hid
    assert sys.modules["pico_uart_hid"] is baseline
    assert baseline.hid is fake_hid


def test_docs_do_not_point_at_unrelated_wiring_setup_doc(repo_root):
    """The physical HIL wiring fixture guide has nothing to do with a
    missing/unbuildable hidapi wheel, so these host-test-only files must
    explain the hidapi fallback inline instead of linking to it.
    """
    unrelated_doc_reference = "self-test-setup" + ".md"
    this_file = repo_root / "host/python/tests/test_hid_optional_dependency.py"
    conftest_file = repo_root / "host/python/tests/conftest.py"
    assert unrelated_doc_reference not in this_file.read_text()
    assert unrelated_doc_reference not in conftest_file.read_text()

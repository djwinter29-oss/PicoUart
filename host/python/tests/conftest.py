"""Pytest fixtures for PicoUart host-tool tests."""

from __future__ import annotations

from pathlib import Path

import pytest

import pico_uart as hid

REPO_ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture
def repo_root() -> Path:
    return REPO_ROOT


@pytest.fixture
def hid_module():
    # hidapi is a native extension; it may be unavailable in sandboxes without a
    # prebuilt wheel for the host Python. Skip HID-dependent tests rather than
    # failing collection for the whole suite.
    if hid.hid is None:
        pytest.skip("hidapi runtime not installed; skipping HID-dependent test")
    return hid

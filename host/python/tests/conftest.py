"""Pytest fixtures for PicoUart host-tool tests."""

from __future__ import annotations

from pathlib import Path

import pytest

import pico_uart_hid as hid

REPO_ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture
def repo_root() -> Path:
    return REPO_ROOT


@pytest.fixture
def hid_module():
    return hid

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Tests for the installed package version export."""

import importlib
from importlib import metadata
from importlib.metadata import PackageNotFoundError, version

import pico_uart


def test_package_exports_its_installed_version():
    try:
        installed_version = version("pico-uart")
    except PackageNotFoundError:
        assert pico_uart.__version__ == "0.0.0-debug"
    else:
        assert pico_uart.__version__ == installed_version


def test_package_uses_debug_version_when_distribution_is_unavailable(monkeypatch):
    def missing_distribution(_name):
        raise PackageNotFoundError

    with monkeypatch.context() as patch:
        patch.setattr(metadata, "version", missing_distribution)
        importlib.reload(pico_uart)
        assert pico_uart.__version__ == "0.0.0-debug"
    importlib.reload(pico_uart)

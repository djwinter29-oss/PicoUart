# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Bundle the canonical dashboard in distributions."""

from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class CustomBuildHook(BuildHookInterface):
    def initialize(self, version, build_data):
        shared_web = Path(self.root).parent / "web"
        assets = shared_web if shared_web.is_dir() else Path(self.root) / "src/pico_uart/web/assets"
        for name in ("index.html", "css/dashboard.css", "js/dashboard.js", "favicon.svg"):
            if not (assets / name).is_file():
                raise FileNotFoundError(f"Missing dashboard asset: {assets / name}")
        if shared_web.is_dir():
            destination = "pico_uart/web/assets" if self.target_name == "wheel" else "src/pico_uart/web/assets"
            for source in shared_web.rglob("*"):
                if source.is_file() and "__pycache__" not in source.parts and source.suffix != ".pyc":
                    relative_path = source.relative_to(shared_web).as_posix()
                    build_data["force_include"][str(source)] = f"{destination}/{relative_path}"
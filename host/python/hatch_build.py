"""Preserve release versions and bundle the canonical dashboard in distributions."""

from email.parser import Parser
import os
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


def get_version():
    version = os.environ.get("PICO_UART_HOST_VERSION")
    metadata = Path(__file__).parent / "PKG-INFO"
    if not version and metadata.is_file():
        version = Parser().parsestr(metadata.read_text(encoding="utf-8"))["Version"]
    return version or "0.0.0.dev0"


class CustomBuildHook(BuildHookInterface):
    def initialize(self, version, build_data):
        shared_web = Path(self.root).parent / "web"
        if shared_web.is_dir():
            destination = "pico_uart/web/assets" if self.target_name == "wheel" else "src/pico_uart/web/assets"
            for source in shared_web.rglob("*"):
                if source.is_file() and "__pycache__" not in source.parts and source.suffix != ".pyc":
                    build_data["force_include"][str(source)] = f"{destination}/{source.relative_to(shared_web).as_posix()}"
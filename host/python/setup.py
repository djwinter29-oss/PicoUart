"""Bundle the shared dashboard in wheels and standalone source distributions."""

from email.parser import Parser
import os
from pathlib import Path
from shutil import copytree, ignore_patterns, rmtree

from setuptools import setup
from setuptools.command.build_py import build_py
from setuptools.command.sdist import sdist


ROOT = Path(__file__).parent
SHARED_WEB = ROOT.parent / "web"


class BuildWithWeb(build_py):
    def run(self):
        web_assets = Path(self.build_lib) / "pico_uart/web/assets"
        if web_assets.exists():
            rmtree(web_assets)
        super().run()
        if SHARED_WEB.is_dir():
            copytree(SHARED_WEB, web_assets, dirs_exist_ok=True,
                     ignore=ignore_patterns("__pycache__", "*.pyc"))


class SourceWithWeb(sdist):
    def make_release_tree(self, base_dir, files):
        super().make_release_tree(base_dir, files)
        if SHARED_WEB.is_dir():
            copytree(SHARED_WEB, Path(base_dir) / "src/pico_uart/web/assets", dirs_exist_ok=True,
                     ignore=ignore_patterns("__pycache__", "*.pyc"))


version = os.environ.get("PICO_UART_HOST_VERSION")
if not version and (ROOT / "PKG-INFO").is_file():
    version = Parser().parsestr((ROOT / "PKG-INFO").read_text(encoding="utf-8"))["Version"]
setup(version=version or "0.0.0.dev0", cmdclass={"build_py": BuildWithWeb, "sdist": SourceWithWeb})
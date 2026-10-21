"""Python distribution contracts without SDKs, attached boards, or network access."""

from email.parser import BytesParser
import os
from pathlib import Path
import shutil
import subprocess
import sys
from zipfile import ZipFile

import pytest


@pytest.mark.parametrize("version", [None, "1.2.3"])
def test_python_wheel_and_standalone_sdist_preserve_version_assets_and_license(repo_root, tmp_path, version):
    project = tmp_path / "checkout" / "host" / "python"
    project.mkdir(parents=True)
    for name in ("pyproject.toml", "hatch_build.py", "README.md", "LICENSE"):
        shutil.copy2(repo_root / "host/python" / name, project / name)
    shutil.copytree(repo_root / "host/python/src", project / "src",
                    ignore=shutil.ignore_patterns("__pycache__", "*.egg-info"))
    shutil.copytree(repo_root / "host/web", project.parent / "web")
    environment = {key: value for key, value in os.environ.items() if key != "PICO_UART_HOST_VERSION"}
    if version:
        environment["PICO_UART_HOST_VERSION"] = version
    expected_version = version or "0.0.0.dev0"
    wheel_directory = tmp_path / "wheel"
    source_directory = tmp_path / "sdist"
    editable_directory = tmp_path / "editable"
    for target, destination in (("wheel", wheel_directory), ("sdist", source_directory), ("editable", editable_directory)):
        destination.mkdir()
        result = subprocess.run(
            [sys.executable, "-c",
             f"from hatchling.build import build_{target}; import sys; build_{target}(sys.argv[1])",
             str(destination)],
            cwd=project, env=environment, capture_output=True, text=True, timeout=30,
        )
        assert result.returncode == 0, result.stdout + result.stderr
    source_archive = next(source_directory.glob("*.tar.gz"))
    unpacked = tmp_path / "unpacked"
    unpack_options = {"filter": "data"} if sys.version_info >= (3, 12) else {}
    shutil.unpack_archive(str(source_archive), str(unpacked), **unpack_options)
    rebuilt_directory = tmp_path / "rebuilt"
    rebuilt_directory.mkdir()
    environment.pop("PICO_UART_HOST_VERSION", None)
    result = subprocess.run(
        [sys.executable, "-c", "from hatchling.build import build_wheel; import sys; build_wheel(sys.argv[1])",
         str(rebuilt_directory)],
        cwd=next(unpacked.iterdir()), env=environment, capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    expected_assets = {
        f"pico_uart/web/assets/{asset.relative_to(repo_root / 'host/web').as_posix()}": asset.read_bytes()
        for asset in (repo_root / "host/web").rglob("*")
        if asset.is_file() and "__pycache__" not in asset.parts and asset.suffix != ".pyc"
    }
    license_text = (project / "LICENSE").read_bytes()
    assert license_text == (repo_root / "LICENSE").read_bytes()
    for directory in (wheel_directory, rebuilt_directory, editable_directory):
        with ZipFile(next(directory.glob("*.whl"))) as wheel:
            metadata_path = f"pico_uart-{expected_version}.dist-info/METADATA"
            metadata = BytesParser().parsebytes(wheel.read(metadata_path))
            assert metadata["Version"] == expected_version
            assert metadata["License-Expression"] == "BSD-3-Clause"
            assert wheel.read(f"pico_uart-{expected_version}.dist-info/licenses/LICENSE") == license_text
            for name, content in expected_assets.items():
                assert wheel.read(name) == content
            assert not any("__pycache__" in name or name.endswith(".pyc") for name in wheel.namelist())
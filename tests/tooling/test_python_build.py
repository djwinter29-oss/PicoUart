# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Python distribution contracts without SDKs, attached boards, or network access."""

from email.parser import BytesParser
from importlib.metadata import version as installed_version
import os
from pathlib import Path
import shutil
import subprocess
import sys
from zipfile import ZipFile

import pytest
from packaging.version import Version
from tomlkit import parse


@pytest.fixture
def python_project(repo_root, tmp_path):
    project = tmp_path / "checkout" / "host" / "python"
    project.mkdir(parents=True)
    for name in ("pyproject.toml", "hatch_build.py", "README.md", "LICENSE"):
        shutil.copy2(repo_root / "host/python" / name, project / name)
    shutil.copytree(repo_root / "host/python/src", project / "src",
                    ignore=shutil.ignore_patterns("__pycache__", "*.egg-info"))
    shutil.copytree(repo_root / "host/python/tests", project / "tests",
                    ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(repo_root / "host/web", project.parent / "web")
    return project


def test_python_build_backend_matches_tested_development_pin(repo_root):
    requirements = [f"{name}=={installed_version(name)}" for name in ("hatchling", "hatch-vcs")]
    project = parse((repo_root / "host/python/pyproject.toml").read_text())
    assert project["build-system"]["requires"] == requirements
    development = project["dependency-groups"]["dev"]
    lines = (repo_root / "host/python/requirements-lock.txt").read_text().splitlines()
    for requirement in requirements:
        assert requirement in development
        assert any(line.removesuffix(" \\") == requirement for line in lines)


@pytest.mark.parametrize("state", ["tagged", "development", "override"])
def test_python_version_comes_from_parent_git_repository(python_project, tmp_path, state):
    root = python_project.parents[1]
    git = ["git", "-c", "user.name=Build Test", "-c", "user.email=build@example.invalid",
           "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false"]
    for arguments in (["init"], ["add", "."], ["commit", "-m", "Initial fixture"],
                      ["tag", "-a", "v1.2.3", "-m", "Release fixture"]):
        result = subprocess.run(git + arguments, cwd=root, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
    environment = {key: value for key, value in os.environ.items()
                   if key not in ("PICO_UART_HOST_VERSION", "SETUPTOOLS_SCM_PRETEND_VERSION")}
    if state == "development":
        result = subprocess.run(git + ["commit", "--allow-empty", "-m", "Next development commit"],
                                cwd=root, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
    elif state == "override":
        environment["SETUPTOOLS_SCM_PRETEND_VERSION"] = "0.0.0.dev42"
    destination = tmp_path / "wheel"
    destination.mkdir()
    result = subprocess.run(
        [sys.executable, "-c", "from hatchling.build import build_wheel; import sys; build_wheel(sys.argv[1])",
         str(destination)],
        cwd=python_project, env=environment, capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    with ZipFile(next(destination.glob("*.whl"))) as wheel:
        metadata_path = next(name for name in wheel.namelist() if name.endswith(".dist-info/METADATA"))
        version = Version(BytesParser().parsebytes(wheel.read(metadata_path))["Version"])
    if state == "development":
        assert version.base_version == "1.2.4"
        assert version.dev == 1
    else:
        assert str(version) == ("0.0.0.dev42" if state == "override" else "1.2.3")


@pytest.mark.parametrize("location", ["shared", "bundled"])
@pytest.mark.parametrize("missing", [None, "index.html", "css/dashboard.css", "js/dashboard.js", "favicon.svg"])
def test_python_build_rejects_missing_dashboard_assets(python_project, tmp_path, location, missing):
    assets = python_project.parent / "web"
    if location == "bundled":
        bundled = python_project / "src/pico_uart/web/assets"
        shutil.move(str(assets), str(bundled))
        assets = bundled
    if missing is None:
        shutil.rmtree(assets)
    else:
        (assets / missing).unlink()
    destination = tmp_path / "wheel"
    destination.mkdir()
    result = subprocess.run(
        [sys.executable, "-c", "from hatchling.build import build_wheel; import sys; build_wheel(sys.argv[1])",
         str(destination)],
        cwd=python_project, capture_output=True, text=True, timeout=30,
    )
    assert result.returncode != 0
    assert "Missing dashboard asset:" in result.stderr
    assert not list(destination.glob("*.whl"))


@pytest.mark.parametrize("version", [None, "1.2.3"])
def test_python_wheel_and_standalone_sdist_preserve_version_assets_and_license(
    repo_root, tmp_path, python_project, version,
):
    project = python_project
    runtime_requirements = parse((project / "pyproject.toml").read_text())["project"]["dependencies"]
    environment = {key: value for key, value in os.environ.items()
                   if key not in ("PICO_UART_HOST_VERSION", "SETUPTOOLS_SCM_PRETEND_VERSION")}
    if version:
        environment["SETUPTOOLS_SCM_PRETEND_VERSION"] = version
    expected_version = version or "0.0.0.dev0"
    wheel_directory = tmp_path / "wheel"
    source_directory = tmp_path / "sdist"
    editable_directory = tmp_path / "editable"
    for target, destination in (
        ("wheel", wheel_directory), ("sdist", source_directory), ("editable", editable_directory),
    ):
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
    source_project = next(unpacked.iterdir())
    assert not (source_project / "tests").exists()
    rebuilt_directory = tmp_path / "rebuilt"
    rebuilt_directory.mkdir()
    environment.pop("SETUPTOOLS_SCM_PRETEND_VERSION", None)
    result = subprocess.run(
        [sys.executable, "-c", "from hatchling.build import build_wheel; import sys; build_wheel(sys.argv[1])",
         str(rebuilt_directory)],
        cwd=source_project, env=environment, capture_output=True, text=True, timeout=30,
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
            assert set(metadata.get_all("Requires-Dist")) == set(runtime_requirements)
            assert wheel.read(f"pico_uart-{expected_version}.dist-info/licenses/LICENSE") == license_text
            for name, content in expected_assets.items():
                assert wheel.read(name) == content
            assert not any(name.startswith(("tests/", "pico_uart/tests/")) for name in wheel.namelist())
            assert not any("__pycache__" in name or name.endswith(".pyc") for name in wheel.namelist())
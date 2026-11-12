# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Python distribution contracts without SDKs, attached boards, or network access."""

from email.parser import BytesParser
from importlib.metadata import version as installed_version
import os
from pathlib import Path
import shlex
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
    for name in ("pyproject.toml", "README.md", "LICENSE"):
        shutil.copy2(repo_root / "host/python" / name, project / name)
    shutil.copytree(repo_root / "host/python/src", project / "src",
                    ignore=shutil.ignore_patterns("__pycache__", "*.egg-info"))
    shutil.copytree(repo_root / "host/python/tests", project / "tests",
                    ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(repo_root / "host/web/src", project / "src/pico_uart/dashboard/assets")
    return project


def test_python_build_backend_matches_tested_development_pin(repo_root):
    requirements = [f"{name}=={installed_version(name)}" for name in ("hatchling", "hatch-vcs")]
    project = parse((repo_root / "host/python/pyproject.toml").read_text())
    assert project["build-system"]["requires"] == requirements
    development = project["dependency-groups"]["dev"]
    for requirement in requirements:
        assert requirement in development
    assert project["tool"]["hatch"]["version"]["source"] == "vcs"
    assert project["tool"]["hatch"]["version"]["raw-options"]["root"] == "../.."
    assert project["tool"]["hatch"]["version"]["raw-options"]["git_describe_command"].startswith(
        "git describe --dirty --tags --long --match"
    )


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


@pytest.mark.parametrize("version", [None, "1.2.3"])
def test_python_wheel_and_standalone_sdist_preserve_version_assets_and_license(
    repo_root, tmp_path, python_project, version,
):
    project = python_project
    metadata_project = parse((project / "pyproject.toml").read_text())["project"]
    runtime_requirements = set(metadata_project["dependencies"])
    assert runtime_requirements == {"flask==3.1.3", "hidapi==0.14.0.post4"}
    assert "optional-dependencies" not in metadata_project
    expected_requirements = runtime_requirements
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
        f"pico_uart/dashboard/assets/{asset.relative_to(repo_root / 'host/web/src').as_posix()}": asset.read_bytes()
        for asset in (repo_root / "host/web/src").rglob("*")
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
            assert set(metadata.get_all("Requires-Dist")) == expected_requirements
            entry_points = wheel.read(f"pico_uart-{expected_version}.dist-info/entry_points.txt").decode()
            assert "pico-uart = pico_uart.main:main" in entry_points
            assert "pico_uart/__main__.py" in wheel.namelist()
            assert wheel.read(f"pico_uart-{expected_version}.dist-info/licenses/LICENSE") == license_text
            if directory != editable_directory:
                for name, content in expected_assets.items():
                    assert wheel.read(name) == content
                assert {name for name in wheel.namelist() if name.startswith("pico_uart/dashboard/assets/")} == set(
                    expected_assets
                )
            assert not any(name.startswith(("tests/", "pico_uart/tests/")) for name in wheel.namelist())
            assert not any("__pycache__" in name or name.endswith(".pyc") for name in wheel.namelist())


@pytest.mark.parametrize("workflow", ["release.yml", "publish-pypi.yml"])
def test_python_wheel_workflows_stage_shared_dashboard_assets(repo_root, workflow):
            content = (repo_root / ".github/workflows" / workflow).read_text()
            staging = "cp -R host/web/src/. host/python/src/pico_uart/dashboard/assets/"
            build = "python -m pip wheel --no-deps host/python --wheel-dir dist"
            assert content.index(staging) < content.index(build)


@pytest.mark.skipif(sys.platform == "win32", reason="run-python.sh is a POSIX shell launcher")
def test_python_launcher_dispatches_to_application_module(repo_root, tmp_path):
    checkout = tmp_path / "checkout"
    host_python = checkout / "host/python"
    launcher_dir = checkout / "tools/host"
    environment_dir = host_python / ".venv"
    python_executable = environment_dir / "bin/python"
    (host_python / "src").mkdir(parents=True)
    launcher_dir.mkdir(parents=True)
    shutil.copytree(repo_root / "host/python/src/pico_uart", host_python / "src/pico_uart")
    shutil.copy2(repo_root / "tools/host/run-python.sh", launcher_dir / "run-python.sh")
    permission_check = launcher_dir / "hid-permissions-check.sh"
    permission_check.write_text("#!/usr/bin/env sh\nexit 0\n")
    permission_check.chmod(0o755)
    python_executable.parent.mkdir(parents=True)
    python_executable.write_text(
        "#!/usr/bin/env sh\n"
        'if [ "$1" = "-c" ] && [ "$2" = "import flask, hid" ]; then exit 0; fi\n'
        f"exec {shlex.quote(sys.executable)} \"$@\"\n"
    )
    python_executable.chmod(0o755)

    result = subprocess.run(
        [str(launcher_dir / "run-python.sh"), "dashboard", "--help"],
        cwd=checkout,
        env={**os.environ, "VENV_DIR": str(environment_dir)},
        capture_output=True,
        text=True,
        timeout=30,
    )

    assert result.returncode == 0, result.stdout + result.stderr
    assert "usage: pico-uart dashboard" in result.stdout
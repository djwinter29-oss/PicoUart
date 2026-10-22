"""Firmware build defaults and external-tool cache reset checks."""

import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX firmware build wrapper")
def test_no_arguments_default_to_pico(tmp_path):
    root = Path(__file__).resolve().parents[2]
    sdk = tmp_path / "sdk"
    (sdk / "external").mkdir(parents=True)
    (sdk / "external/pico_sdk_import.cmake").touch()
    shim = tmp_path / "cmake"
    shim.write_text(
        f"#!{sys.executable}\n"
        "import sys\n"
        "if '-S' in sys.argv:\n"
        "    assert '-DPICO_BOARD=pico' in sys.argv, 'no-argument build must select Pico/RP2040'\n"
    )
    shim.chmod(0o700)
    env = {
        **os.environ,
        "PATH": str(tmp_path) + os.pathsep + os.environ["PATH"],
        "PICO_SDK_PATH": str(sdk),
        "BUILD_DIR": str(tmp_path / "build"),
    }
    env.pop("PICO_BOARD", None)
    result = subprocess.run(
        ["sh", str(root / "tools/firmware/build.sh")],
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX firmware build wrapper")
@pytest.mark.parametrize("cause", ["version-change", "orphaned-picotool", "unchanged"])
def test_build_reset_clears_picotool_stamps(tmp_path, cause):
    root = Path(__file__).resolve().parents[2]
    sdk = tmp_path / "sdk"
    (sdk / "external").mkdir(parents=True)
    (sdk / "external/pico_sdk_import.cmake").touch()
    build = tmp_path / "build"
    stamp = build / "picotool/src/picotoolBuild-stamp/picotoolBuild-configure"
    stamp.parent.mkdir(parents=True)
    stamp.touch()
    dependency_cache = build / "_deps/picotool-build/CMakeCache.txt"
    dependency_cache.parent.mkdir(parents=True)
    if cause != "orphaned-picotool":
        dependency_cache.touch()
    (build / "CMakeCache.txt").write_text(
        f"CMAKE_TOOLCHAIN_FILE:FILEPATH={sdk}/toolchain.cmake\n"
        "CMAKE_GENERATOR:INTERNAL=Ninja\n"
        "PICO_BOARD:STRING=pico\n"
        f"PICO_UART_VERSION:STRING={'old' if cause == 'version-change' else '0.0.0-dev'}\n"
        "PICO_UART_ALLOW_HID_RESET:BOOL=ON\n"
    )
    shim = tmp_path / "cmake"
    shim.write_text(
        f"#!{sys.executable}\n"
        "import pathlib, sys\n"
        "if '-S' in sys.argv:\n"
        "    build = pathlib.Path(sys.argv[sys.argv.index('-B') + 1])\n"
        f"    reset = {cause != 'unchanged'!r}\n"
        "    assert (build / 'picotool').exists() != reset, 'incorrect external-project stamp cleanup'\n"
        "    assert (build / '_deps').exists() != reset, 'incorrect dependency build cleanup'\n"
    )
    shim.chmod(0o700)
    result = subprocess.run(
        [
            "sh", str(root / "tools/firmware/build.sh"),
            "--build-dir", str(build), "--pico-sdk-path", str(sdk), "--generator", "Ninja",
        ],
        env={**os.environ, "PATH": str(tmp_path) + os.pathsep + os.environ["PATH"]},
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert ("resetting generated CMake state" in result.stdout) == (cause != "unchanged")

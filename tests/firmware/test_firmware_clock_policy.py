"""Build-time clock overrides are rejected; family detection remains SDK-driven."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys

import pytest


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX shell wrapper")
@pytest.mark.parametrize("argument", ["--system-clock-khz", "--unsafe-overclock"])
def test_build_wrapper_rejects_removed_clock_flags(repo_root, tmp_path, argument):
    sdk = tmp_path / "sdk"
    (sdk / "external").mkdir(parents=True)
    (sdk / "external/pico_sdk_import.cmake").write_text("")
    command = [
        "sh",
        str(repo_root / "tools/firmware/build.sh"),
        "--board",
        "pico",
        "--pico-sdk-path",
        str(sdk),
        "--build-dir",
        str(tmp_path / "build"),
        argument,
    ]
    if argument == "--system-clock-khz":
        command.append("250000")
    completed = subprocess.run(command, cwd=repo_root, capture_output=True, text=True)
    assert completed.returncode == 1
    assert "Build-time clocks were removed" in completed.stderr


def _write_stub_sdk(sdk_dir, platform):
    (sdk_dir / "external").mkdir(parents=True)
    platform_line = f'set(PICO_PLATFORM "{platform}")\n' if platform is not None else ""
    (sdk_dir / "external/pico_sdk_import.cmake").write_text(
        platform_line + "function(pico_sdk_init)\nendfunction()\n"
        "function(pico_generate_pio_header)\n"
        '  message(FATAL_ERROR "Configuration policy accepted")\n'
        "endfunction()\n"
    )


@pytest.mark.skipif(
    sys.platform == "win32" or shutil.which("cmake") is None,
    reason="Linux firmware configuration requires cmake and a native toolchain",
)
@pytest.mark.parametrize(
    ("board", "platform", "expected"),
    [
        ("pico", "rp2040", "Configuration policy accepted"),
        ("pico_w", "rp2040", "Configuration policy accepted"),
        ("pico2", "rp2350", "Configuration policy accepted"),
        ("pico2_w", "rp2350", "Configuration policy accepted"),
        ("acme_falcon", "rp2350", "Configuration policy accepted"),
    ],
)
def test_cmake_accepts_supported_platforms_without_clock_overrides(repo_root, tmp_path, board, platform, expected):
    sdk = tmp_path / "sdk"
    _write_stub_sdk(sdk, platform)
    completed = subprocess.run(
        [
            "cmake",
            "-S",
            str(repo_root / "firmware"),
            "-B",
            str(tmp_path / "build"),
            f"-DPICO_SDK_PATH={sdk}",
            f"-DPICO_BOARD={board}",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )
    assert completed.returncode != 0
    assert expected in completed.stderr, completed.stdout + completed.stderr


@pytest.mark.skipif(
    sys.platform == "win32" or shutil.which("cmake") is None,
    reason="Linux firmware configuration requires cmake and a native toolchain",
)
@pytest.mark.parametrize(
    ("cmake_arg", "expected"),
    [
        ("-DPICO_UART_SYSTEM_CLOCK_KHZ=250000", "Build-time clock overrides were removed"),
        ("-DPICO_UART_ALLOW_UNSAFE_OVERCLOCK=ON", "Build-time clock overrides were removed"),
    ],
)
def test_cmake_rejects_removed_clock_cache_variables(repo_root, tmp_path, cmake_arg, expected):
    sdk = tmp_path / "sdk"
    _write_stub_sdk(sdk, "rp2040")
    completed = subprocess.run(
        [
            "cmake",
            "-S",
            str(repo_root / "firmware"),
            "-B",
            str(tmp_path / "build"),
            f"-DPICO_SDK_PATH={sdk}",
            "-DPICO_BOARD=pico",
            cmake_arg,
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )
    assert completed.returncode != 0
    assert expected in completed.stderr, completed.stdout + completed.stderr


@pytest.mark.skipif(
    sys.platform == "win32" or shutil.which("cmake") is None,
    reason="Linux firmware configuration requires cmake and a native toolchain",
)
@pytest.mark.parametrize(
    ("board", "platform"),
    [
        ("mystery_board", None),
        ("mystery_board", "esp32"),
    ],
)
def test_cmake_fails_closed_for_unresolved_platform(repo_root, tmp_path, board, platform):
    sdk = tmp_path / "sdk"
    _write_stub_sdk(sdk, platform)
    completed = subprocess.run(
        [
            "cmake",
            "-S",
            str(repo_root / "firmware"),
            "-B",
            str(tmp_path / "build"),
            f"-DPICO_SDK_PATH={sdk}",
            f"-DPICO_BOARD={board}",
        ],
        cwd=repo_root,
        capture_output=True,
        text=True,
    )
    assert completed.returncode != 0
    assert "not a recognized rp2040/rp2350 platform" in completed.stderr, completed.stdout + completed.stderr

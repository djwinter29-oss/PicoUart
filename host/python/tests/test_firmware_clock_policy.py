"""Clock ceilings must allow the Pico 2 300 MHz override, not widen RP2040.

Target-family discrimination (RP2040 vs RP2350) is driven entirely by the
SDK's own PICO_PLATFORM metadata (see firmware/CMakeLists.txt), never by
PICO_BOARD name matching. These tests cover: the build.sh wrapper deferring
ceiling enforcement for board names it does not recognize, CMake enforcing
the real ceiling/rated clock from SDK-supplied PICO_PLATFORM for both
canonical and custom (non "pico"-prefixed) boards, CMake failing closed when
PICO_PLATFORM is missing or unrecognized, and parity between the canonical
board aliases (pico/pico_w, pico2/pico2_w).
"""

import json
import os
import shutil
import subprocess
import sys

import pytest


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX executable build-tool shim")
@pytest.mark.parametrize(("board", "clock", "accepted"), [
    ("pico", "400000", True), ("pico", "400001", False), ("pico", "500000", False),
    ("pico_w", "400000", True), ("pico_w", "400001", False),
    ("pico2", "500000", True), ("pico2", "500001", False),
    ("pico2", "1000000", False), ("pico2", "999999999999999999999", False),
    ("pico2_w", "500000", True), ("pico2_w", "500001", False),
    # A board name the wrapper does not recognize has no known family here;
    # it must defer ceiling enforcement to CMake (which reads real SDK
    # metadata) instead of guessing from the name. The wrapper still rejects
    # malformed/oversized input on its own.
    ("acme_falcon", "999999", True), ("acme_falcon", "9999999", False),
    ("acme_falcon", "not-a-number", False),
])
def test_build_wrapper_clock_ceiling(repo_root, tmp_path, board, clock, accepted):
    sdk = tmp_path / "sdk"
    (sdk / "external").mkdir(parents=True)
    (sdk / "external/pico_sdk_import.cmake").write_text("")
    shim = tmp_path / "cmake"
    log = tmp_path / "cmake.jsonl"
    shim.write_text(f"#!{sys.executable}\n" + '''import json, os, sys
with open(os.environ["CMAKE_LOG"], "a") as stream:
    stream.write(json.dumps(sys.argv[1:]) + "\\n")
''')
    shim.chmod(0o700)
    completed = subprocess.run(
        ["sh", str(repo_root / "tools/firmware/build.sh"), "--board", board,
         "--system-clock-khz", clock, "--unsafe-overclock", "--pico-sdk-path", str(sdk),
         "--build-dir", str(tmp_path / "build")],
        cwd=repo_root,
        env={**os.environ, "PATH": str(tmp_path) + os.pathsep + os.environ["PATH"],
             "CMAKE_LOG": str(log)},
        capture_output=True, text=True,
    )
    assert (completed.returncode == 0) == accepted, completed.stderr
    if accepted:
        calls = [json.loads(line) for line in log.read_text().splitlines()]
        assert len(calls) == 2
        assert f"-DPICO_UART_SYSTEM_CLOCK_KHZ={clock}" in calls[0]
        assert "-DPICO_UART_ALLOW_UNSAFE_OVERCLOCK=ON" in calls[0]
    else:
        assert not log.exists()


def _write_stub_sdk(sdk_dir, platform):
    """Write a minimal pico_sdk_import.cmake stub that stands in for the real
    SDK's board-header-driven PICO_PLATFORM resolution (firmware/CMakeLists.txt
    reads PICO_PLATFORM, not PICO_BOARD, to pick the RP2040/RP2350 family), then
    stops at the first real SDK call made after CMake's own clock guards.
    """
    (sdk_dir / "external").mkdir(parents=True)
    platform_line = f'set(PICO_PLATFORM "{platform}")\n' if platform is not None else ""
    (sdk_dir / "external/pico_sdk_import.cmake").write_text(
        platform_line +
        "function(pico_sdk_init)\nendfunction()\n"
        "function(pico_generate_pio_header)\n"
        "  message(FATAL_ERROR \"Clock policy accepted\")\n"
        "endfunction()\n"
    )


@pytest.mark.skipif(sys.platform == "win32" or shutil.which("cmake") is None,
                    reason="Linux firmware configuration requires cmake and a native toolchain")
@pytest.mark.parametrize(("board", "platform", "clock", "unsafe", "expected"), [
    ("pico", "rp2040", "400000", "ON", "Clock policy accepted"),
    ("pico", "rp2040", "500000", "ON", "no greater than 400000"),
    ("pico_w", "rp2040", "400000", "ON", "Clock policy accepted"),
    ("pico2", "rp2350", "500000", "ON", "Clock policy accepted"),
    ("pico2", "rp2350", "500001", "ON", "no greater than 500000"),
    ("pico2_w", "rp2350", "500000", "ON", "Clock policy accepted"),
    ("pico2", "rp2350", "500000", "OFF", "use the rated 150000"),
    ("pico2", "rp2350", "150000", "OFF", "Clock policy accepted"),
    # Custom (non "pico"-prefixed) board names: the family comes solely from
    # PICO_PLATFORM, so these must use the real RP2350/RP2040 ceilings too.
    ("acme_falcon", "rp2350", "500000", "ON", "Clock policy accepted"),
    ("acme_falcon", "rp2350", "500001", "ON", "no greater than 500000"),
    ("widget2040", "rp2040", "400000", "ON", "Clock policy accepted"),
    ("widget2040", "rp2040", "400001", "ON", "no greater than 400000"),
])
def test_cmake_clock_policy_without_sdk_build(repo_root, tmp_path, board, platform, clock, unsafe, expected):
    sdk = tmp_path / "sdk"
    _write_stub_sdk(sdk, platform)
    # Stop at the first SDK build operation, after the real CMake clock guards.
    completed = subprocess.run(
        ["cmake", "-S", str(repo_root / "firmware"), "-B", str(tmp_path / "build"),
         f"-DPICO_SDK_PATH={sdk}", f"-DPICO_BOARD={board}",
         f"-DPICO_UART_SYSTEM_CLOCK_KHZ={clock}", f"-DPICO_UART_ALLOW_UNSAFE_OVERCLOCK={unsafe}"],
        cwd=repo_root, capture_output=True, text=True,
    )
    assert completed.returncode != 0
    assert expected in completed.stderr, completed.stdout + completed.stderr


@pytest.mark.skipif(sys.platform == "win32" or shutil.which("cmake") is None,
                    reason="Linux firmware configuration requires cmake and a native toolchain")
@pytest.mark.parametrize(("board", "platform"), [
    ("mystery_board", None),  # SDK never populated PICO_PLATFORM at all
    ("mystery_board", "esp32"),  # SDK populated an unrecognized platform
])
def test_cmake_fails_closed_for_unresolved_platform(repo_root, tmp_path, board, platform):
    sdk = tmp_path / "sdk"
    _write_stub_sdk(sdk, platform)
    completed = subprocess.run(
        ["cmake", "-S", str(repo_root / "firmware"), "-B", str(tmp_path / "build"),
         f"-DPICO_SDK_PATH={sdk}", f"-DPICO_BOARD={board}"],
        cwd=repo_root, capture_output=True, text=True,
    )
    assert completed.returncode != 0
    assert "not a recognized rp2040/rp2350 platform" in completed.stderr, completed.stdout + completed.stderr


@pytest.mark.skipif(sys.platform == "win32" or shutil.which("cmake") is None,
                    reason="Linux firmware configuration requires cmake and a native toolchain")
@pytest.mark.parametrize(("platform_line", "clock", "expected"), [
    ("rp2350", "500001", "no greater than 500000"),
    ("rp2040", "400001", "no greater than 400000"),
])
def test_real_sdk_custom_board_header_ceiling(repo_root, tmp_path, platform_line, clock, expected):
    """End-to-end check against the real, checked-in pico-sdk: a custom board
    header (not named "pico"/"pico2") that declares PICO_PLATFORM is enough
    for CMake to pick the right family and ceiling, with no board-name
    matching involved. Configuring stops at the clock guard, before
    project()/compiler detection, so this stays fast and needs no toolchain.
    """
    sdk_path = repo_root / ".pico-sdk"
    if not (sdk_path / "external/pico_sdk_import.cmake").is_file():
        pytest.skip("Real pico-sdk checkout not available; run tools/firmware/setup-sdk-env.sh")
    headers = tmp_path / "boards"
    headers.mkdir()
    base_header = "pico2.h" if platform_line == "rp2350" else "pico.h"
    (headers / "acme_falcon.h").write_text(
        "#ifndef _BOARDS_ACME_FALCON_H\n"
        "#define _BOARDS_ACME_FALCON_H\n"
        f"pico_board_cmake_set(PICO_PLATFORM, {platform_line})\n"
        f'#include "boards/{base_header}"\n'
        "#endif\n"
    )
    completed = subprocess.run(
        ["cmake", "-S", str(repo_root / "firmware"), "-B", str(tmp_path / "build"),
         f"-DPICO_SDK_PATH={sdk_path}", "-DPICO_BOARD=acme_falcon",
         f"-DPICO_BOARD_HEADER_DIRS={headers}",
         f"-DPICO_UART_SYSTEM_CLOCK_KHZ={clock}", "-DPICO_UART_ALLOW_UNSAFE_OVERCLOCK=ON"],
        cwd=repo_root, capture_output=True, text=True, timeout=120,
    )
    assert completed.returncode != 0
    assert expected in completed.stderr, completed.stdout + completed.stderr

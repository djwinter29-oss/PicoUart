"""Clock ceilings must allow the Pico 2 300 MHz override, not widen RP2040."""

import json
import os
import shutil
import subprocess
import sys

import pytest


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX executable build-tool shim")
@pytest.mark.parametrize(("board", "clock", "accepted"), [
    ("pico", "400000", True), ("pico", "400001", False), ("pico", "500000", False),
    ("pico2", "500000", True), ("pico2", "500001", False),
    ("pico2", "1000000", False), ("pico2", "999999999999999999999", False),
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
        assert "must be no greater than" in completed.stderr


@pytest.mark.skipif(sys.platform == "win32" or shutil.which("cmake") is None,
                    reason="Linux firmware configuration requires cmake and a native toolchain")
@pytest.mark.parametrize(("board", "clock", "unsafe", "expected"), [
    ("pico", "400000", "ON", "Clock policy accepted"),
    ("pico", "500000", "ON", "no greater than 400000"),
    ("pico2", "500000", "ON", "Clock policy accepted"),
    ("pico2", "500001", "ON", "no greater than 500000"),
    ("pico2", "500000", "OFF", "use the rated 150000"),
    ("pico2", "150000", "OFF", "Clock policy accepted"),
])
def test_cmake_clock_policy_without_sdk_build(repo_root, tmp_path, board, clock, unsafe, expected):
    sdk = tmp_path / "sdk"
    (sdk / "external").mkdir(parents=True)
    (sdk / "external/pico_sdk_import.cmake").write_text(
        "function(pico_sdk_init)\nendfunction()\n"
        "function(pico_generate_pio_header)\n"
        "  message(FATAL_ERROR \"Clock policy accepted\")\n"
        "endfunction()\n"
    )
    # Stop at the first SDK build operation, after the real CMake clock guards.
    completed = subprocess.run(
        ["cmake", "-S", str(repo_root / "firmware"), "-B", str(tmp_path / "build"),
         f"-DPICO_SDK_PATH={sdk}", f"-DPICO_BOARD={board}",
         f"-DPICO_UART_SYSTEM_CLOCK_KHZ={clock}", f"-DPICO_UART_ALLOW_UNSAFE_OVERCLOCK={unsafe}"],
        cwd=repo_root, capture_output=True, text=True,
    )
    assert completed.returncode != 0
    assert expected in completed.stderr, completed.stdout + completed.stderr

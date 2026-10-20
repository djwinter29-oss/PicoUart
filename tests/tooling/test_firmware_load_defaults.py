"""Build/load board defaults must resolve the same ELF, without contacting hardware."""

import json
import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX firmware wrappers")
@pytest.mark.parametrize(
    ("board_env", "board_arg", "board", "target"),
    [
        (None, None, "pico", "rp2040"),
        ("", None, "pico", "rp2040"),
        ("pico2", None, "pico2", "rp2350"),
        ("pico2", "pico", "pico", "rp2040"),
    ],
)
def test_loader_builds_and_programs_the_same_default_elf(tmp_path, board_env, board_arg, board, target):
    root = Path(__file__).resolve().parents[2]
    sdk = tmp_path / "sdk"
    (sdk / "external").mkdir(parents=True)
    (sdk / "external/pico_sdk_import.cmake").touch()
    cmake = tmp_path / "cmake"
    cmake.write_text(
        f"#!{sys.executable}\n"
        "from pathlib import Path\n"
        "import os, sys\n"
        "if '-S' in sys.argv:\n"
        "    build = Path(sys.argv[sys.argv.index('-B') + 1])\n"
        "    assert build.name == 'firmware-' + os.environ['EXPECTED_BOARD']\n"
        "    assert '-DPICO_BOARD=' + os.environ['EXPECTED_BOARD'] in sys.argv\n"
        "    artifact = Path(os.environ['MOCK_ELF'])\n"
        "    artifact.parent.mkdir(parents=True, exist_ok=True)\n"
        "    artifact.touch()\n"
    )
    cmake.chmod(0o700)
    openocd = tmp_path / "openocd"
    openocd.write_text(
        f"#!{sys.executable}\n"
        "from pathlib import Path\n"
        "import json, os, sys\n"
        "Path(os.environ['MOCK_OPENOCD_LOG']).write_text(json.dumps(sys.argv[1:]))\n"
    )
    openocd.chmod(0o700)
    log = tmp_path / "openocd.json"
    env = {
        **os.environ,
        "PATH": str(tmp_path) + os.pathsep + os.environ["PATH"],
        "PICO_SDK_PATH": str(sdk),
        "GENERATOR": "Ninja",
        "OPENOCD_EXE": str(openocd),
        "MOCK_OPENOCD_LOG": str(log),
        "PICO_DEBUG_PROBE_SERIAL": "offline-test-probe",
        "EXPECTED_BOARD": board,
    }
    for name in ("BUILD_DIR", "PICO_BOARD", "PICO_OPENOCD_TARGET"):
        env.pop(name, None)
    if board_env is not None:
        env["PICO_BOARD"] = board_env
    command = ["sh", str(root / "tools/firmware/load.sh")]
    if board_arg:
        command.extend(["--board", board_arg])
    shim_dir = tmp_path / "checkout/tools/firmware"
    shim_dir.mkdir(parents=True)
    for name in ("load.sh", "build.sh"):
        (shim_dir / name).write_bytes((root / "tools/firmware" / name).read_bytes())
        (shim_dir / name).chmod(0o700)
    expected_elf = tmp_path / f"checkout/build/firmware-{board}/pico_uart.elf"
    env["MOCK_ELF"] = str(expected_elf)
    command[1] = str(shim_dir / "load.sh")
    result = subprocess.run(command, env=env, capture_output=True, text=True, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    arguments = json.loads(log.read_text())
    assert f"target/{target}.cfg" in arguments
    assert f"program {{{expected_elf}}} verify reset exit" in arguments

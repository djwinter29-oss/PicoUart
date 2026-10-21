"""USB permission failures must not be mistaken for a stalled Debug Probe."""

import os
from pathlib import Path
import subprocess
import sys

import pytest


@pytest.mark.skipif(sys.platform == "win32" or os.geteuid() == 0, reason="Requires POSIX non-root permissions")
@pytest.mark.parametrize("serial", ["", "offline-test-probe"])
@pytest.mark.parametrize("sudo_available,elevated_exit", [(False, 0), (True, 0), (True, 1)])
def test_hidden_probe_permission_failure(tmp_path, serial, sudo_available, elevated_exit):
    root = Path(__file__).resolve().parents[2]
    usb = tmp_path / "usb"
    node = usb / "003/003"
    node.parent.mkdir(parents=True)
    node.touch(mode=0o400)
    loader = tmp_path / "load.sh"
    loader.write_text((root / "tools/firmware/load.sh").read_text().replace("/dev/bus/usb/", f"{usb}/"))
    elf = tmp_path / "firmware.elf"
    elf.touch()
    log = tmp_path / "calls"
    scripts = {
        "lsusb": "echo 'Bus 003 Device 003: ID 2e8a:000c Raspberry Pi Debug Probe'",
        "openocd": (
            'echo openocd >> "$CALL_LOG"\n'
            'if [ "${ELEVATED:-0}" = 1 ]; then exit "$ELEVATED_EXIT"; fi\n'
            "echo 'Error: unable to find a matching CMSIS-DAP device' >&2\nexit 1"
        ),
        "sudo": (
            'shift\nif [ "$1" = true ]; then exit "$SUDO_CHECK"; fi\n'
            'echo sudo >> "$CALL_LOG"\nELEVATED=1 "$@"'
        ),
        "usbreset": 'echo usbreset >> "$CALL_LOG"\nexit 1',
    }
    for name, body in scripts.items():
        executable = tmp_path / name
        executable.write_text("#!/bin/sh\n" + body + "\n")
        executable.chmod(0o700)
    env = {
        **os.environ,
        "PATH": str(tmp_path) + os.pathsep + os.environ["PATH"],
        "OPENOCD_EXE": str(tmp_path / "openocd"),
        "PICO_DEBUG_PROBE_SERIAL": serial,
        "PICO_DEBUG_PROBE_VID": "0x2e8a",
        "PICO_DEBUG_PROBE_PID": "0x000c",
        "CALL_LOG": str(log),
        "SUDO_CHECK": "0" if sudo_available else "1",
        "ELEVATED_EXIT": str(elevated_exit),
    }
    result = subprocess.run(
        ["sh", str(loader), "--board", "pico", "--skip-build", "--elf", str(elf)],
        env=env, capture_output=True, text=True, check=False,
    )
    assert result.returncode == (elevated_exit if sudo_available else 1), result.stdout + result.stderr
    assert log.read_text().splitlines() == (["openocd", "sudo", "openocd"] if sudo_available else ["openocd"])
    assert "resetting stalled" not in result.stderr
    assert ("Retrying OpenOCD with sudo" if sudo_available else "Debug Probe USB access denied") in result.stderr

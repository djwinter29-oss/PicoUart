"""Compile and run the real firmware `system_init_clock` against mock SDK
headers to pin the no-voltage-write policy: both boards must only call
`set_sys_clock_khz` at the requested clock, with no `hardware/vreg.h` include
and no vreg symbol anywhere in the link (any voltage write or bypass fails
the build or the recorded-clock check below).
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

import pytest

# ponytail: mocks only the subset of pico-sdk/hardware headers that system.c
# uses; if system.c starts using more of the real SDK API, extend these.
_MOCK_STDLIB_H = """
#ifndef MOCK_PICO_STDLIB_H
#define MOCK_PICO_STDLIB_H
#include <stdbool.h>
#include <stdlib.h>
#define hard_assert(x) do { if (!(x)) { abort(); } } while (0)
static inline void tight_loop_contents(void) {}
#endif
"""

_MOCK_CLOCKS_H = """
#ifndef MOCK_HARDWARE_CLOCKS_H
#define MOCK_HARDWARE_CLOCKS_H
#include <stdbool.h>
#include <stdint.h>
extern uint32_t g_requested_clock_khz;
extern int g_set_sys_clock_khz_calls;
bool set_sys_clock_khz(uint32_t clock_khz, bool required);
#endif
"""

_MOCK_WATCHDOG_H = """
#ifndef MOCK_HARDWARE_WATCHDOG_H
#define MOCK_HARDWARE_WATCHDOG_H
#include <stdbool.h>
#include <stdint.h>
static inline void watchdog_enable(uint32_t timeout_ms, bool pause_on_debug) {
    (void)timeout_ms; (void)pause_on_debug;
}
static inline void watchdog_update(void) {}
static inline void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc; (void)sp; (void)delay_ms;
}
#endif
"""

# NOTE: no hardware/vreg.h mock is provided. If system.c (re)gains a
# `#include "hardware/vreg.h"`, compilation fails with a missing-header error,
# which is the intended detection signal for a reintroduced voltage write.

_MOCK_RUNTIME_C = """
#include <stdint.h>
#include <stdbool.h>
uint32_t g_requested_clock_khz;
int g_set_sys_clock_khz_calls;
bool set_sys_clock_khz(uint32_t clock_khz, bool required) {
    (void)required;
    g_requested_clock_khz = clock_khz;
    g_set_sys_clock_khz_calls++;
    return true;
}
"""

_MAIN_C = """
#include <stdio.h>
#include "driver/system.h"
extern unsigned int g_requested_clock_khz;
extern int g_set_sys_clock_khz_calls;
int main(void) {
    system_init_clock();
    printf("%u %d\\n", g_requested_clock_khz, g_set_sys_clock_khz_calls);
    return 0;
}
"""


def _write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def _read_nm_symbols(nm, artifact):
    """Run `nm` on `artifact`, raising CalledProcessError if `nm` itself
    fails. A failure here must never be read as "no symbols found"; without
    check=True a broken/failing nm would make every vreg-symbol assertion
    below pass vacuously on empty output, silently accepting the failure
    instead of reporting it (see test_nm_failure_is_not_silently_accepted).

    `nm` may be a path string or an argv prefix (e.g. ``[sys.executable,
    script]``) so the failure-injection test stays portable on Windows, where
    a POSIX shebang script is not a valid Win32 application.
    """
    cmd = [*nm, str(artifact)] if isinstance(nm, (list, tuple)) else [nm, str(artifact)]
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


@pytest.mark.skipif(
    shutil.which("gcc") is None and shutil.which("cc") is None,
    reason="native C compiler required to exercise real system.c",
)
@pytest.mark.parametrize(
    ("board_defines", "clock_khz"),
    [
        ([], 125000),  # RP2040 rated
        ([], 250000),  # RP2040 development overclock
        (["PICO_RP2350B"], 150000),  # RP2350 rated
        (["PICO_RP2350B"], 300000),  # RP2350 development overclock
    ],
    ids=["rp2040-rated", "rp2040-250mhz", "rp2350-rated", "rp2350-300mhz"],
)
def test_system_init_clock_never_writes_voltage(repo_root, tmp_path, board_defines, clock_khz):
    mock_sdk = tmp_path / "mock_sdk"
    _write(mock_sdk / "pico" / "stdlib.h", _MOCK_STDLIB_H)
    _write(mock_sdk / "hardware" / "clocks.h", _MOCK_CLOCKS_H)
    _write(mock_sdk / "hardware" / "watchdog.h", _MOCK_WATCHDOG_H)

    (tmp_path / "mock_runtime.c").write_text(_MOCK_RUNTIME_C)
    (tmp_path / "main.c").write_text(_MAIN_C)

    system_c = repo_root / "firmware" / "src" / "driver" / "system.c"
    source_text = system_c.read_text(encoding="utf-8")
    # Defense in depth alongside the missing-header compile failure: the real
    # source must not reference vreg at all.
    assert "vreg" not in source_text.lower()

    # Windows linkers emit `<name>.exe` even when `-o` omits the suffix; nm
    # and CreateProcess need the real on-disk path.
    exe = tmp_path / ("system_clock_test.exe" if os.name == "nt" else "system_clock_test")
    cmd = [
        shutil.which("cc") or shutil.which("gcc"),
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-DPICO_UART_SYSTEM_CLOCK_KHZ={clock_khz}u",
        *(f"-D{define}" for define in board_defines),
        f"-I{mock_sdk}",
        f"-I{repo_root / 'firmware' / 'src'}",
        str(system_c),
        str(tmp_path / "mock_runtime.c"),
        str(tmp_path / "main.c"),
        "-o",
        str(exe),
    ]
    compiled = subprocess.run(cmd, capture_output=True, text=True)
    assert compiled.returncode == 0, compiled.stderr
    assert exe.is_file(), f"compiler did not produce {exe}"

    # The link must not pull in any symbol with "vreg" in its name. nm itself
    # is optional here (mock native test); if present it must succeed.
    nm = shutil.which("nm")
    if nm:
        assert "vreg" not in _read_nm_symbols(nm, exe).lower()

    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    recorded_khz, call_count = run.stdout.split()
    assert int(recorded_khz) == clock_khz
    assert int(call_count) == 1


@pytest.mark.parametrize("build_dir", os.environ.get("PICO_UART_VOLTAGE_BUILD_DIRS", "").split(os.pathsep))
def test_real_sdk_startup_voltage_policy(build_dir):
    """Opt-in artifact check; unlike the mocks above, inspect actual SDK output."""
    if not build_dir:
        if not os.environ.get("PICO_UART_VOLTAGE_BUILD_DIRS"):
            pytest.skip("set PICO_UART_VOLTAGE_BUILD_DIRS to completed firmware build directories")
        pytest.fail("empty build directory in PICO_UART_VOLTAGE_BUILD_DIRS")
    build = Path(build_dir).resolve()
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    entries = [entry for entry in commands if Path(entry["file"]).name == "runtime_init_clocks.c"]
    assert len(entries) == 1, "expected the SDK startup source in the firmware target"
    entry = entries[0]
    argv = entry.get("arguments") or shlex.split(entry["command"])
    assert "-DSYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=0" in argv
    output_index = argv.index("-o")
    obj = Path(entry["directory"]) / argv[output_index + 1]
    artifacts = (obj, build / "pico_uart.elf")
    for artifact in artifacts:
        assert artifact.is_file(), f"missing firmware artifact: {artifact}"
    # Use the actual cross compiler, generated headers, and defines to resolve
    # SDK defaults; just checking an application mock misses pre-main startup.
    preprocess = [arg for i, arg in enumerate(argv) if i not in (output_index, output_index + 1) and arg != "-c"]
    result = subprocess.run(
        [*preprocess, "-E", "-dM"], cwd=entry["directory"], capture_output=True, text=True, check=True
    )
    assert "#define SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST 0" in result.stdout.splitlines()
    nm = shutil.which("arm-none-eabi-nm")
    assert nm, "ARM nm required for the real firmware artifact check"
    forbidden = {"vreg_set_voltage", "vreg_disable_voltage_limit"}
    for artifact in artifacts:
        symbols = _read_nm_symbols(nm, artifact)
        names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
        assert not names & forbidden, f"voltage mutation in {artifact}: {names & forbidden}"


def test_nm_failure_is_not_silently_accepted(tmp_path):
    """A failing nm must raise, not be mistaken for "no vreg symbols found".
    Without check=True in _read_nm_symbols, a broken nm binary (empty
    stdout, nonzero exit) would make the vreg-symbol assertions in both
    tests above pass vacuously instead of reporting the real failure.
    """
    # Drive the failing tool through the current interpreter: a POSIX shebang
    # script is not a valid Win32 application (WinError 193).
    failing_nm = tmp_path / "failing_nm.py"
    failing_nm.write_text("import sys\nsys.exit(1)\n", encoding="utf-8")
    with pytest.raises(subprocess.CalledProcessError):
        _read_nm_symbols(
            [sys.executable, str(failing_nm)],
            tmp_path / "irrelevant-artifact",
        )

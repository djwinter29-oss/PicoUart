"""Compile and run the real firmware `system_init_clock` against mock SDK
headers to pin the no-voltage-write policy: both boards must only call
`set_sys_clock_khz` at the requested clock, with no `hardware/vreg.h` include
and no vreg symbol anywhere in the link (any voltage write or bypass fails
the build or the recorded-clock check below).
"""

from __future__ import annotations

import shutil
import subprocess
import sys

import pytest

pytestmark = pytest.mark.skipif(
    shutil.which("gcc") is None and shutil.which("cc") is None,
    reason="native C compiler required to exercise real system.c",
)

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
    source_text = system_c.read_text()
    # Defense in depth alongside the missing-header compile failure: the real
    # source must not reference vreg at all.
    assert "vreg" not in source_text.lower()

    exe = tmp_path / "system_clock_test"
    cmd = [
        shutil.which("cc") or shutil.which("gcc"),
        "-std=c11", "-Wall", "-Wextra", "-Werror",
        f"-DPICO_UART_SYSTEM_CLOCK_KHZ={clock_khz}u",
        *(f"-D{define}" for define in board_defines),
        f"-I{mock_sdk}",
        f"-I{repo_root / 'firmware' / 'src'}",
        str(system_c),
        str(tmp_path / "mock_runtime.c"),
        str(tmp_path / "main.c"),
        "-o", str(exe),
    ]
    compiled = subprocess.run(cmd, capture_output=True, text=True)
    assert compiled.returncode == 0, compiled.stderr

    # The link must not pull in any symbol with "vreg" in its name.
    nm = shutil.which("nm")
    if nm:
        symbols = subprocess.run([nm, str(exe)], capture_output=True, text=True).stdout
        assert "vreg" not in symbols.lower()

    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    recorded_khz, call_count = run.stdout.split()
    assert int(recorded_khz) == clock_khz
    assert int(call_count) == 1

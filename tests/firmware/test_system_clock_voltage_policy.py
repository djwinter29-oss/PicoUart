"""Compile and run the real firmware `system_init_clock` against mock SDK
headers to pin the no-voltage-write policy: defaults never override SDK
startup and valid profiles request an exact clock, with no `hardware/vreg.h` include
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
#define __not_in_flash_func(x) x
#define PICO_OK 0
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

_MOCK_ADDRESSMAP_H = """
#include <stdint.h>
extern uint8_t g_sector[4096];
#define XIP_BASE ((uintptr_t)g_sector)
"""

_MOCK_WATCHDOG_H = """
#ifndef MOCK_HARDWARE_WATCHDOG_H
#define MOCK_HARDWARE_WATCHDOG_H
#include <stdbool.h>
#include <stdint.h>
extern bool g_watchdog_recovery;
static inline bool watchdog_enable_caused_reboot(void) { return g_watchdog_recovery; }
static inline void watchdog_enable(uint32_t timeout_ms, bool pause_on_debug) {
    (void)timeout_ms; (void)pause_on_debug;
    g_watchdog_recovery = false;
}
static inline void watchdog_update(void) {}
static inline void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms) {
    (void)pc; (void)sp; (void)delay_ms;
}
#endif
"""

_MOCK_FLASH_H = """
#include <stddef.h>
#include <stdint.h>
#define FLASH_PAGE_SIZE 256u
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);
"""

_MOCK_SAFE_FLASH_H = """
#include <stdint.h>
int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms);
"""

# NOTE: no hardware/vreg.h mock is provided. If system.c (re)gains a
# `#include "hardware/vreg.h"`, compilation fails with a missing-header error,
# which is the intended detection signal for a reintroduced voltage write.

_MOCK_RUNTIME_C = """
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
uint8_t g_sector[4096];
bool g_watchdog_recovery = WATCHDOG_RECOVERY;
uint32_t g_requested_clock_khz;
int g_set_sys_clock_khz_calls;
int g_flash_calls;
int g_flash_result;
bool g_flash_corrupt;
void flash_range_erase(uint32_t offset, size_t count) {
    assert(offset == 0 && count == sizeof(g_sector));
    memset(g_sector, 255, count);
    g_flash_calls++;
}
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
    assert(offset == 0 && count == 256);
    memcpy(g_sector, data, count);
    if (g_flash_corrupt) g_sector[4095] = 0;
}
int flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms) {
    assert(timeout_ms == 1000);
    if (!g_flash_result) func(param);
    return g_flash_result;
}
bool set_sys_clock_khz(uint32_t clock_khz, bool required) {
    if (required) __builtin_abort();
    g_requested_clock_khz = clock_khz;
    g_set_sys_clock_khz_calls++;
    return CLOCK_AVAILABLE;
}
"""

_MAIN_C = """
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "platform/system.h"
extern uint8_t g_sector[4096];
extern unsigned int g_requested_clock_khz;
extern int g_set_sys_clock_khz_calls;
extern int g_flash_calls, g_flash_result;
extern bool g_flash_corrupt;
static void put_u32(unsigned offset, uint32_t value) {
    for (unsigned i=0; i<4; i++) g_sector[offset+i] = value >> (8*i);
}
static uint32_t get_u32(const uint8_t *data) {
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}
static uint32_t fixture_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (unsigned int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ UINT32_MAX;
}
int main(void) {
    memset(g_sector, 255, sizeof(g_sector));
    if (PROFILE >= 0) {
        memset(g_sector, 0, 36);
        memcpy(g_sector, "PUCF", 4);
        g_sector[4] = 1;
        g_sector[6] = 32;
        put_u32(8, 36);
        put_u32(32, PROFILE);
        uint8_t protected[32];
        memcpy(protected, g_sector, 12);
        memcpy(protected+12, g_sector+16, 20);
        put_u32(12, fixture_crc32(protected, 32) ^ CORRUPT);
    }
    system_watchdog_enable();
    system_init_clock();
    const boot_config_status_t *config = system_boot_config_status();
    printf("%u %d %u %u\\n", g_requested_clock_khz, g_set_sys_clock_khz_calls,
           (unsigned)config->reason, (unsigned)config->active_profile);
    boot_config_status_t boot = *config;
    int boot_clock_calls = g_set_sys_clock_khz_calls;
    unsigned boot_clock_khz = g_requested_clock_khz;
    assert(!system_save_clock_profile(0xffffffffu));
    unsigned profile = PICO_RP2350 ? 2 : 1;
    assert(!system_save_clock_profile(PICO_RP2350 ? 1 : 2));
    memset(g_sector, 255, sizeof(g_sector));
    g_flash_result = -1;
    assert(!system_save_clock_profile(profile));
    assert(g_flash_calls == 0 && g_sector[0] == 255);
    g_flash_result = 0;
    g_flash_corrupt = true;
    assert(!system_save_clock_profile(profile));
    assert(g_flash_calls == 1);
    g_flash_corrupt = false;
    assert(system_save_clock_profile(profile));
    assert(g_flash_calls == 2);
    assert(system_save_clock_profile(profile));
    assert(g_flash_calls == 2);
    assert(memcmp(g_sector, "PUCF", 4) == 0);
    assert(get_u32(g_sector + 32) == profile);
    uint8_t protected[32];
    memcpy(protected, g_sector, 12);
    memcpy(protected + 12, g_sector + 16, 20);
    assert(fixture_crc32(protected, sizeof(protected)) == get_u32(g_sector + 12));
    assert(system_save_clock_profile(0));
    assert(get_u32(g_sector + 32) == BOOT_CLOCK_DEFAULT);
    assert(memcmp(config, &boot, sizeof(boot)) == 0);
    assert(g_set_sys_clock_khz_calls == boot_clock_calls && g_requested_clock_khz == boot_clock_khz);
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
    ("rp2350", "profile", "corrupt", "recovery", "available", "clock_khz", "reason", "active"),
    [
        (0, -1, 0, 0, 1, 0, 1, 0),
        (1, -1, 0, 0, 1, 0, 1, 0),
        (0, 0, 0, 0, 1, 0, 0, 0),
        (1, 0, 0, 0, 1, 0, 0, 0),
        (0, 1, 0, 0, 1, 250000, 0, 1),
        (1, 2, 0, 0, 1, 280000, 0, 2),
        (0, 3, 0, 0, 1, 200000, 0, 3),
        (1, 3, 0, 0, 1, 200000, 0, 3),
        (0, 4, 0, 0, 1, 150000, 0, 4),
        (1, 4, 0, 0, 1, 150000, 0, 4),
        (0, 2, 0, 0, 1, 0, 5, 0),
        (1, 1, 0, 0, 1, 0, 5, 0),
        (0, 1, 1, 0, 1, 0, 3, 0),
        (0, 1, 0, 1, 1, 0, 6, 0),
        (1, 2, 0, 1, 1, 0, 6, 0),
        (0, 1, 0, 0, 0, 250000, 7, 0),
    ],
)
def test_system_init_clock_never_writes_voltage(
    repo_root, tmp_path, rp2350, profile, corrupt, recovery, available, clock_khz, reason, active
):
    mock_sdk = tmp_path / "mock_sdk"
    _write(mock_sdk / "pico" / "stdlib.h", _MOCK_STDLIB_H)
    _write(mock_sdk / "hardware" / "clocks.h", _MOCK_CLOCKS_H)
    _write(mock_sdk / "hardware" / "watchdog.h", _MOCK_WATCHDOG_H)
    _write(mock_sdk / "hardware" / "regs" / "addressmap.h", _MOCK_ADDRESSMAP_H)
    _write(mock_sdk / "hardware" / "flash.h", _MOCK_FLASH_H)
    _write(mock_sdk / "pico" / "flash.h", _MOCK_SAFE_FLASH_H)

    (tmp_path / "mock_runtime.c").write_text(_MOCK_RUNTIME_C)
    (tmp_path / "main.c").write_text(_MAIN_C)

    system_c = repo_root / "firmware" / "src" / "platform" / "system.c"
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
        f"-DPICO_RP2350={rp2350}",
        f"-DPROFILE={profile}",
        f"-DCORRUPT={corrupt}",
        f"-DWATCHDOG_RECOVERY={recovery}",
        f"-DCLOCK_AVAILABLE={available}",
        "-DPICO_FLASH_SIZE_BYTES=4096",
        "-DPICO_UART_CONFIG_FLASH_SIZE=4096",
        "-DPICO_UART_CONFIG_FLASH_OFFSET=0",
        f"-I{mock_sdk}",
        f"-I{repo_root / 'firmware' / 'src'}",
        str(system_c),
        str(repo_root / "firmware/src/platform/boot_config.c"),
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
    recorded_khz, call_count, recorded_reason, recorded_active = run.stdout.split()
    assert int(recorded_khz) == clock_khz
    assert int(call_count) == (1 if clock_khz else 0)
    assert int(recorded_reason) == reason
    assert int(recorded_active) == active


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

# Firmware and Repository Testing

This guide summarizes development boundaries and test levels. Detailed build and load steps are in
[Firmware Build and Configuration](../../firmware/build-and-config.md); canonical test commands and HIL procedures are
in the [Test Documentation Index](../tests/README.md) and [Repository Tools](../../tools/README.md).

## Design Boundaries

Keep board pin mapping separate from USB and UART transport logic. See [Architecture](../design/architecture.md), the
[UART subsystem design](../design/uart/README.md), [Hardware Setup](../hardware-setup.md), and the
[CDC/HID overview](../design/usb/cdc-hid-overview.md) for their respective responsibilities and constraints.

## Test Scope

Validation is split across native firmware logic tests, host and cross-project contract tests, and physical HIL. The
root [pytest configuration](../../pyproject.toml) discovers repository tests; host packages and the HIL runner also have
project-specific configuration. Use the test index to choose the applicable level and command rather than treating one
suite as a substitute for another.

## Optional Timing Profiles

Use a fresh build directory for each measured variant because CMake caches initial compiler flags:

```sh
CFLAGS='-DPICO_UART_PROFILE_TIMING=1' tools/firmware/build.sh --board pico --build-dir build/profile-default
CFLAGS='-DPICO_UART_PROFILE_TIMING=1 -DPICO_UART_USB_CDC_BRIDGE_PASS_BUDGET=512' \
	tools/firmware/build.sh --board pico --build-dir build/profile-budget512
```

The optional counters `service_load_max_pass_us[2]`, `service_load_max_gap_us[2]`, and
`uart_driver_max_service_gap_us[6]` are SWD-readable observed maxima in microseconds. Locate them with
`arm-none-eabi-nm --defined-only <elf>`. Authorized profiling can zero those arrays immediately before traffic, then
read them without halting the cores through OpenOCD's `echo [read_memory <address> 32 <count>]`. Per-port timing
excludes intentional stopped time. Instrumentation perturbs execution, and maxima include host/control activity in the
measurement interval; they are not proven worst-case execution times or real-time bounds.

Change only one tuning macro per variant. The bridge budget and PIO TX DMA threshold have compile-time range checks.
Compare verified traffic, block latency, per-port service gaps, ring peaks, and faults under the same conditions.
The serial stress runner's timing records include verified block count, maximum block duration, and total block time;
these are host-observed round trips, not isolated UART wire latency. Retain defaults when a short comparison does not
show a clear benefit. Return to an uninstrumented image for final HIL; profiling is disabled by default and adds no
HID protocol fields. See the [dated session/tuning check](../tests/records/2026-10-22-cdc-uart-session-validation.md).

## Buffer Capacity Measurement

Build each intended board with the normal firmware wrapper before comparing allocations. For each exact ELF, retain its
SHA-256, build options, linker map, and these read-only measurements:

```sh
sha256sum build/firmware-pico/pico_uart.elf
arm-none-eabi-size build/firmware-pico/pico_uart.elf
arm-none-eabi-nm -S --size-sort build/firmware-pico/pico_uart.elf
```

Inspect `uart_driver_state`, `uart_driver_rx_storage`, `_cdcd_itf`, and `usb_cdc_ports` in the symbol list and map. With debug
information, GDB `p sizeof(...)` can distinguish backend instance size, per-port stride, and enclosing state size.
Include the separate RX storage symbol when comparing UART allocations; the runtime-state symbol alone excludes it.
Do not equate the difference between ring payload and driver allocation with unused buffers: alignment padding,
metadata, and backend-union layout all contribute. BSS and data totals also exclude stack growth and dynamic allocations;
retain the memory-region and reserved-stack information from the map when assessing available headroom.

For runtime evidence, use the [concurrent fixture plan](../tests/hil-fixture-test-plan.md) on the identified board and
exact flashed image. Collect all six channels' peaks and dropped/error counters during the run, not just after close.
HID's ring peak combines RX/TX and rounds to 16-byte blocks; it cannot establish independent RX, TX, or CDC FIFO peaks.
Use authorized, non-halting SWD snapshots of the individual rings and CDC statistics when that distinction is needed.
Peaks are boot-lifetime values, so use a fresh boot per workload rather than attributing an old maximum to a new rate.

Include bounded host stalls and burst traffic, not only the USB-paced write/read workload. At 8N1, an RX capacity of
`N` bytes holds at most `N * 10 / baud` seconds of continuous input before accounting for existing occupancy and service
delay. A low peak in a paced test therefore does not establish safe stall tolerance. Preserve capacities, RX DMA
power-of-two/alignment constraints, bridge snapshot checks, and fairness bounds until representative measurements
justify a smaller allocation. The existing [tuning record](../tests/records/2026-10-22-cdc-uart-session-validation.md)
supports retaining the current scheduling defaults; it does not qualify reduced buffers.

## CI And Hardware Scope

CI validates host behavior and firmware builds. It has no physical board or wiring fixture and cannot prove UART
signaling, USB enumeration, DMA timing, or multicore behavior under load. Physical acceptance and exact-artifact release
gates are defined in the [hardware test documentation](../tests/README.md) and
[release guide](../deployment/releasing.md).

## Cursor Cloud Environment

This section describes the provisioned Cursor Cloud VM snapshot, not requirements or hardware availability for every
local checkout. The snapshot has no attached board, debug probe, or UART fixture; use the non-hardware checks above.

### Snapshot Packages

These packages are installed once and captured in the snapshot. Do not apt-install them from the update script:

- `ninja-build`, `gcc-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`, `libnewlib-arm-none-eabi`, `gcovr`, and
	`cppcheck` (apt).
- `libstdc++-14-dev` (apt): the snapshot's default `/usr/bin/c++` is clang and selects the gcc-14 toolchain directory.
	Without it, the native picotool build fails with `cannot find -lstdc++`, even with `libstdc++-13-dev` present.
- `python3-venv`, `python3-dev`, `libusb-1.0-0-dev`, `libhidapi-dev`, and `libhidapi-hidraw0` (apt) for the host venv
	and HID library.

### Update Script

Run this update script in Bash; it refreshes the checkout environment and does not start a service:

```bash
set -euo pipefail
python3 -m venv host/python/.venv
host/python/.venv/bin/python -m pip install --require-hashes -r host/python/requirements-lock.txt
host/python/.venv/bin/python -m pip install --no-deps --no-build-isolation -e host/python
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0 --sdk-revision 98a542c1a62fb549ffb5d66a3e5892b06276b670
```

The Python commands install locked development dependencies and `pico-uart` editable; see the
[host developer setup](host-tools.md#python-environment-setup).
`setup-sdk-env.sh` clones the pinned SDK into gitignored `.pico-sdk/` if missing, and otherwise confirms the pin.
Keep the SDK line: a fresh checkout does not carry `.pico-sdk/`.

Ubuntu's `/usr/bin/python3` is PEP 668 externally managed. Point the host-test wrapper at the venv rather than its
default system interpreter:

```sh
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh
host/python/.venv/bin/python -m pico_uart --help
```

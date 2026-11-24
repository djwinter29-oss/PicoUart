# Firmware Development and Tuning

This guide covers firmware work beyond a plain build: where each kind of test fits, how to profile timing, and how to
judge buffer sizes. Building and loading are covered in [Firmware Build and Configuration](../../firmware/build-and-config.md).

## Test Scope

There are three levels, and none substitutes for another:

| Level | Proves | Where |
| --- | --- | --- |
| Native C tests | Firmware logic without a board: rings, line-coding policy, control ownership, bridge spans | [firmware/tests](../../firmware/tests/README.md) |
| Host and contract tests | Python/.NET clients, HID binary layouts, tooling, docs checks | [tools/README.md](../../tools/README.md) |
| Hardware-in-the-loop | Real UART signalling, USB enumeration, DMA timing, multicore load | [Test documentation](../tests/README.md) |

CI runs the first two and builds the firmware. It has no board, so it cannot prove anything about physical behavior.

## Optional Timing Profiles

Timing instrumentation is off by default. Enable it in a separate build directory for each variant, because CMake
caches the initial compiler flags:

```sh
CFLAGS='-DPICO_UART_PROFILE_TIMING=1' tools/firmware/build.sh --board pico --build-dir build/profile-default
CFLAGS='-DPICO_UART_PROFILE_TIMING=1 -DPICO_UART_USB_CDC_BRIDGE_PASS_BUDGET=512' \
	tools/firmware/build.sh --board pico --build-dir build/profile-budget512
```

This adds three SWD-readable arrays of observed maxima, in microseconds: `service_load_max_pass_us[2]`,
`service_load_max_gap_us[2]`, and `uart_driver_max_service_gap_us[6]`. Find them with `arm-none-eabi-nm`, zero them
just before the traffic run, and read them without halting the cores using OpenOCD
`echo [read_memory <address> 32 <count>]`. They are observed maxima under instrumentation, not proven worst cases.

Change one tuning macro per variant, and compare verified traffic, block latency, service gaps, ring peaks, and faults
under the same conditions. Keep the defaults unless a comparison shows a clear win, and go back to an uninstrumented
image for final HIL. The current defaults are backed by the
[2026-10-22 tuning record](../tests/records/2026-10-22-cdc-uart-session-validation.md).

## Buffer Capacity Measurement

Ring sizes are the biggest RAM cost in the firmware, but they are also what absorbs host stalls. Before shrinking them,
measure both the allocation and the runtime use.

**Allocation.** For each exact ELF, keep its SHA-256, build options, and linker map, plus:

```sh
sha256sum build/firmware-pico/pico_uart.elf
arm-none-eabi-size build/firmware-pico/pico_uart.elf
arm-none-eabi-nm -S --size-sort build/firmware-pico/pico_uart.elf
```

Look at `uart_driver_state`, `uart_driver_rx_storage`, `_cdcd_itf`, and `usb_cdc_ports`. The RX storage is a separate
symbol, so include it. Alignment padding and metadata account for the gap between payload size and allocation. That gap
is not unused buffer space.

**Runtime use.** Run the [concurrent fixture plan](../tests/hil-fixture-test-plan.md) on a freshly booted board and read
each channel's observed peaks and drop counters from HID diagnostics during the run. Peaks last for the whole boot, so
reboot between workloads. Include host stalls and bursts, not only paced traffic. At 8N1, an RX ring of `N` bytes holds
only `N * 10 / baud` seconds of input. A low peak in a paced test therefore says nothing about stall tolerance.

Keep the current capacities until measurements like these justify a smaller value.

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
host/python/.venv/bin/python -m pip install --group host/python/pyproject.toml:dev
host/python/.venv/bin/python -m pip install --no-build-isolation -e host/python
. tools/firmware/setup-sdk-env.sh --sdk-version 2.3.0 --sdk-revision 98a542c1a62fb549ffb5d66a3e5892b06276b670
```

The Python commands install the `dev` dependency group and `pico-uart` editable; see the
[host developer setup](host-tools.md#python-environment-setup).
`setup-sdk-env.sh` clones the pinned SDK into gitignored `.pico-sdk/` if missing, and otherwise confirms the pin.
Keep the SDK line: a fresh checkout does not carry `.pico-sdk/`.

Ubuntu's `/usr/bin/python3` is PEP 668 externally managed. Point the host-test wrapper at the venv rather than its
default system interpreter:

```sh
PYTHON_EXE=host/python/.venv/bin/python tools/validation/run-host-tests.sh
host/python/.venv/bin/python -m pico_uart.main --help
```

# Firmware Testing

This guide covers firmware work beyond a plain build: timing profiles and buffer measurements. For build and load
instructions, see [Firmware Build and Configuration](../../firmware/build-and-config.md); for test commands, use the
[hardware-free test checklist](../tests/README.md#without-hardware).
For the provisioned Cursor Cloud snapshot setup, see the [Cursor Cloud environment guide](cursor-cloud.md).

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
under the same conditions, then go back to an uninstrumented image for final HIL. The
[HIL records' tuning notes](../tests/records/README.md#tuning-notes) summarize previous experiments.

## Buffer Capacity Measurement

Ring sizes are the biggest RAM cost in the firmware, but they are also what absorbs host stalls. Before shrinking them,
measure both the allocation and the runtime use.

### Allocation

For each exact ELF, keep its SHA-256, build options, and linker map, plus:

```sh
sha256sum build/firmware-pico/pico_uart.elf
arm-none-eabi-size build/firmware-pico/pico_uart.elf
arm-none-eabi-nm -S --size-sort build/firmware-pico/pico_uart.elf
```

Look at `uart_driver_state`, `uart_driver_rx_storage`, `_cdcd_itf`, and `usb_cdc_ports`. The RX storage is a separate
symbol, so include it. Alignment padding and metadata account for the gap between payload size and allocation. That gap
is not unused buffer space.

### Runtime Use

Run the [concurrent fixture plan](../tests/hil-fixture-test-plan.md) on a freshly booted board and read
each channel's observed peaks and drop counters from HID diagnostics during the run. Peaks last for the whole boot, so
reboot between workloads. Include host stalls and bursts, not only paced traffic. At 8N1, an RX ring of `N` bytes holds
only `N * 10 / baud` seconds of input. A low peak in a paced test therefore says nothing about stall tolerance.

Keep the current capacities until measurements like these justify a smaller value.

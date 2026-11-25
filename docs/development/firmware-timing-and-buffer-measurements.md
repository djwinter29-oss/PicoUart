# Firmware Timing and Buffer Measurements

This guide covers optional timing profiles and buffer measurements for comparing firmware tuning candidates. Build and
load instructions are in [Firmware Build and Configuration](../../firmware/build-and-config.md).

## Optional Timing Profiles

Timing instrumentation is off by default. Enable it in a separate build directory for each variant, because CMake
caches the initial compiler flags:

```sh
CFLAGS='-DPICO_UART_PROFILE_TIMING=1' tools/firmware/build.sh --board pico --build-dir build/profile-default
CFLAGS='-DPICO_UART_PROFILE_TIMING=1 -DPICO_UART_USB_CDC_BRIDGE_PASS_BUDGET=512' \
  tools/firmware/build.sh --board pico --build-dir build/profile-budget512
```

This adds three SWD-readable arrays of observed maxima, in microseconds: `service_load_max_pass_us[2]`,
`service_load_max_gap_us[2]`, and `uart_driver_max_service_gap_us[6]`. Find their addresses in the ELF:

```sh
arm-none-eabi-nm -n build/profile-default/pico_uart.elf | grep -E \
  'service_load_max_(pass|gap)_us|uart_driver_max_service_gap_us'
```

Start OpenOCD with the same probe and target settings as the
[firmware loading guide](../../firmware/build-and-config.md#debug-probe-loading-and-recovery). For a Pico, use:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 1000" -c "init"
```

For Pico 2, use `target/rp2350.cfg`; add `-c "adapter serial <serial>"` when selecting a specific probe. In another
terminal, connect to the OpenOCD console with `telnet localhost 4444`. Replace each placeholder below with the
matching address reported by `nm`. Zero the arrays immediately before traffic, then read them afterward:

```tcl
write_memory <service_load_max_pass_us_address> 32 0 2
write_memory <service_load_max_gap_us_address> 32 0 2
write_memory <uart_driver_max_service_gap_us_address> 32 0 6
echo [read_memory <service_load_max_pass_us_address> 32 2]
echo [read_memory <service_load_max_gap_us_address> 32 2]
echo [read_memory <uart_driver_max_service_gap_us_address> 32 6]
```

Use counts 2, 2, and 6 for the three arrays, respectively. Do not halt or reset the cores during the run. These maxima
come from one instrumented run; they are not worst-case timing or release evidence. Use them to compare candidates as
described below. Previous results are in
[the matched refill experiments](../tests/records/2026-10-23-uart-cdc-review-validation.md#matched-refill-experiments).

## Comparing Tuning Candidates

Use current defaults as the baseline. Compare baseline and candidate images on both boards under the same clock, wiring,
payloads, rates, host, and test durations. Change one setting per candidate, repeat the baseline to measure run-to-run
variation, and retain image hashes and raw samples.

Measure small-payload round trips and six-stream throughput, plus service gaps, ring peaks, fault deltas, and reopen
behavior. Compare timing distributions as well as maxima and means; the benchmark does not report percentiles. Finish
with payload acceptance on uninstrumented firmware. TX-pipelining changes must preserve CTS stalls, byte order, and the
shifter-idle check before reconfiguration.

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

Run the [concurrent fixture plan](../tests/hil-fixture-test-plan.md) on a freshly booted board and read each channel's
observed peaks and drop counters from HID diagnostics during the run. Peaks last for the whole boot, so reboot between
workloads. Include host stalls and bursts alongside paced traffic. At 8N1, an RX ring of `N` bytes holds only
`N * 10 / baud` seconds of input. A paced test with low peaks does not show how the ring handles a stalled host.

Keep the current capacities until measurements like these justify a smaller value.

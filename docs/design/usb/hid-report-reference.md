# HID Report Reference

PicoUart exposes one vendor-defined USB HID interface named `Status Monitor`. It provides diagnostics for the six UART
bridges plus narrowly scoped board controls. UART data and line configuration remain on the matching USB CDC (`ttyACM`)
interface. All UART receivers start at board boot and bridge data whenever their matching CDC interface is connected.
DTR state is reported for monitoring only.

The HID interface uses vendor usage page `0xFF00`, vendor usage `0x01`, no boot protocol, and a 63-byte status report
alongside compact diagnostic and command feature reports. The device is identified as USB `cafe:4010` (the project's
unallocated lab identity, permitted for published project artifacts) and has one HID interface after the twelve CDC
control/data interfaces. The USB and HID product name is `PicoUart`. CDC interface strings advertise backend
limits: `CDC0 HW` / `CDC1 HW` and `CDC2`-`CDC5 PIO 8N1`.

## Ownership

| Operation                                                     | USB interface              | Current behavior                                                                                                                                                                                                                                                             |
| ------------------------------------------------------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| UART data                                                     | CDC0 through CDC5          | Transfers bytes to and from the matching UART.                                                                                                                                                                                                                               |
| Baud, data bits, parity, stop bits                            | CDC line-coding request    | Parsed from `SET_LINE_CODING` and queued to the matching UART backend. TinyUSB accepts the USB transfer before firmware validation; rejected or unsupported requests set health bit 2 (`control_error`) instead of stalling the CDC control pipe. PIO ports accept 8N1 only. |
| Health, traffic, ring peak, temperature, and firmware version | HID                        | Read-only monitoring data.                                                                                                                                                                                                                                                   |
| Applied UART settings, GPIOs, and fault history | HID feature reports 10-15 | Read-only snapshots. These reports cannot change line coding or pin mapping. |
| Toggle default board LED                                      | HID command feature report | Toggles `PICO_DEFAULT_LED_PIN` when the selected board defines one.                                                                                                                                                                                                          |
| Reset board                                                   | HID command feature report | Disabled by default; trusted lab builds may enable arm (`3`) then reset (`2`) within 2 seconds with `PICO_UART_ALLOW_HID_RESET=1`. Enabled builds set board-status `reserved0` bit 0; the host `reset` command fails closed when that bit is clear.                          |

HID must not be used to select a UART, set baud rate, change GPIO mapping, or alter ring-buffer behavior. The three
command values are board-scoped only.

## Report IDs

| Report ID | Type    | Direction              | Payload  | Purpose                                             |
| --------- | ------- | ---------------------- | -------- | --------------------------------------------------- |
| `1`       | Input   | Device to host         | 63 bytes | Periodic compact status report.                     |
| `3`       | Feature | Host reads from device | 8 bytes  | Temperature estimate and firmware semantic version. |
| `4`       | Feature | Host writes to device  | 1 byte   | Board-control command.                              |
| `5`       | Feature | Host reads from device | 25 bytes | Cumulative UART-to-USB RX dropped-byte counts.      |
| `6`       | Feature | Host reads from device | 6 bytes  | MCU identity and current system clock frequency.    |
| `7`       | Feature | Host reads from device | 49 bytes | Product and USB serial / board UID.                 |
| `8`       | Feature | Host reads from device | 21 bytes | Uptime, per-core service load, and reserved RAM.     |
| `9`       | Feature | Host reads from device | 2 bytes  | Board LED availability, manual state, and output.   |
| `10`-`15` | Feature | Host reads from device | 59 bytes | Applied settings, GPIOs and fault history for CDC0-CDC5. |

Report IDs `1`, `3`, and `5` share layout version `15`. Report ID `6` uses an independent layout version, currently `1`.
Reports `7` and `8` have independent layout versions, currently `1` and `2` respectively.
Report `9` uses independent layout version `1`.
Reports `10`-`15` use independent layout version `1`; older firmware does not implement them.

Report ID bytes are managed by the HID transport and are not included in the payload layouts below. Status is 63 bytes
so Report ID + payload fit in one full-speed interrupt packet (64 bytes). The device attempts to publish report ID `1`
periodically while its HID IN endpoint is ready. Reports are not queued when the endpoint is busy.

## Reports 10-15: Channel Diagnostics

Report `10 + channel` addresses a single channel without a selector command or any change to UART ownership.
All integers are little-endian. Existing report layouts and command values are unchanged.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Independent layout version, `1`. |
| 1 | 1 | `health` | Sampled driver bits 0-3 and RX historical flags 6-7; no CDC-open/backend bits. |
| 2 | 4 | `baud_rate` | Successfully applied controller baud rate, not the last requested baud. Zero if unavailable. |
| 6 | 1 | `data_bits` | Successfully applied data-bit count. |
| 7 | 1 | `stop_bits` | Successfully applied stop-bit count, 1 or 2. |
| 8 | 1 | `parity` | None=0, odd=1, even=2. |
| 9 | 1 | `tx_pin` | Board-configured TX GPIO; 255 means unassigned. |
| 10 | 1 | `rx_pin` | Board-configured RX GPIO; 255 means unassigned. |
| 11 | 12 | `init_failed` | Fault history record. |
| 23 | 12 | `control_error` | Fault history record. |
| 35 | 12 | `rx_overrun` | Fault history record, count measured in dropped bytes. |
| 47 | 12 | `rx_error` | Fault history record, count measured in controller error observations. |

Each history record contains a saturating `uint32_t count` and a `uint64_t last_seen_ms` (boot uptime at detection).
A zero count means no observation; a nonzero count can legitimately have timestamp zero during the first boot millisecond.
The USB core samples diagnostics approximately every 100 ms, including when the HID IN endpoint is not ready, and on
diagnostic reads. Timestamps describe **detection**, not the exact instant of a wire error. Counts are retained until
firmware initialization; counter resets are treated as a new baseline without subtracting accumulated history.

`init_failed` and `control_error` count observed deasserted-to-asserted transitions, not individual failed requests.
Multiple transitions between samples can coalesce. RX error counts come from the backend's sticky error observations;
multiple bad frames before a hardware/PIO error flag is harvested can also coalesce. These are not per-byte error counts.
`rx_overrun` includes both retired and still-pending overwritten bytes without counting retirement twice.
At counter saturation, the count cannot distinguish further events.

RX flags in report 1 are historical latches based on nonzero counters, not live signal-level indicators. A closed CDC
port does not stop the UART receiver. Floating/undriven RX, a break, incorrect baud/framing, or wiring problems are
possible causes, but this report cannot identify which one occurred. See [UART pinout](../../uart-pinout.md).
There is no device-side clear command. Dashboards display fault history read-only, without local acknowledgement.

## Report ID 1: Status

All multi-byte values are little-endian. Channels use logical UART port IDs, so channel `0` is CDC0/UART0 and channel
`5` is CDC5/UART5.

| Offset | Size | Field        | Meaning                                                     |
| ------ | ---: | ------------ | ----------------------------------------------------------- |
| 0      |    1 | `signature0` | ASCII `P` (`0x50`).                                         |
| 1      |    1 | `version`    | Shared report layout version.                               |
| 2      |    1 | `sequence`   | Increments after each successfully published status report. |
| 3      |   60 | `channel[6]` | Six consecutive 10-byte CDC/UART channel snapshots.         |

Each `channel` record has the following layout:

| Relative offset | Size | Field                        | Meaning                                                        |
| --------------- | ---: | ---------------------------- | -------------------------------------------------------------- |
| 0               |    1 | `health`                     | UART status flags plus CDC-open and PIO-backend flags.         |
| 1               |    1 | `ring_high_watermark_blocks` | Largest RX or TX ring occupancy, rounded up to 16-byte blocks. |
| 2               |    2 | `controller_tx_bytes`        | UART controller TX byte delta.                                 |
| 4               |    2 | `controller_rx_bytes`        | UART controller RX byte delta.                                 |
| 6               |    2 | `cdc_tx_bytes`               | Device-to-host CDC byte delta.                                 |
| 8               |    2 | `cdc_rx_bytes`               | Host-to-device CDC byte delta.                                 |

The byte counters are changes since the preceding successfully published status report. They saturate at `65535`; a
saturated value means the actual delta was at least that large. The ring peak is cumulative from boot and saturates at
`4080` bytes.

Those deltas and `sequence` advance only after a successful interrupt IN publish. A control-pipe `GET_REPORT` of input
report 1 returns the same uncommitted snapshot and does not advance `sequence` or the delta baseline. Use the interrupt
stream (`pico-uart monitor`) when the delta must be committed. `SET_LINE_CODING` can still complete on the bus when
firmware rejects it; watch health bit 2 on that interrupt stream.

### `health` Bits

| Bit | Meaning                                                                                                                                                                                  |
| --: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
|   0 | UART backend is initialized and ready.                                                                                                                                                   |
|   1 | Backend initialization failed.                                                                                                                                                           |
|   2 | The most recent control request failed (invalid or unsupported CDC line coding, deferred-apply timeout, or backend reject). USB `SET_LINE_CODING` may still have completed successfully. |
|   3 | A line-coding control request is pending.                                                                                                                                                |
|   4 | Host has opened the matching CDC interface (DTR asserted).                                                                                                                               |
|   5 | The matching UART uses PIO; clear for hardware UART.                                                                                                                                     |
|   6 | UART RX data has been overwritten since boot; drain the CDC interface or apply flow control.                                                                                             |
|   7 | The hardware UART has observed an RSR error since the post-boot baseline, or the PIO UART has observed a stop-bit framing error since initialization.                                    |

## Line-coding rejects

Hosts typically treat CDC `SET_LINE_CODING` as fire-and-forget. TinyUSB may complete the transfer before firmware
applies or rejects the request, so hosts should monitor health bit 3 (`control_pending`) and then bit 2
(`control_error`). PIO accepts 8N1 only. Request ordering, timeout, and retry semantics are specified in the
[Control Plane Design](../control-plane-design.md); host monitoring is covered by the
[CDC/HID Overview](cdc-hid-overview.md).

PIO UART ports remain 8N1-only. Hardware UART0/UART1 accept supported baud/data/parity/stop combinations within firmware
bounds (50-3 000 000 baud, 5-8 data bits, 1/2 stop, none/odd/even parity).

## Report ID 3: Board Status

Request feature report ID `3` to read the internal RP2 temperature-sensor estimate and the firmware semantic version.
Tag `v1.2.3` builds advertise `1.2.3` here. The USB device descriptor `bcdDevice` carries only major.minor as BCD (so
`1.2.3` -> `0x0102`, commonly shown as `1.02` / `1.2`).

| Offset | Size | Field                              | Meaning                                                                                                                                          |
| ------ | ---: | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0      |    1 | `version`                          | Shared report layout version.                                                                                                                    |
| 1      |    1 | `reserved0`                        | Board-status flags. Bit 0 is set when HID arm/reset is compiled in (`PICO_UART_ALLOW_HID_RESET=1`). Other bits remain reserved and must be zero. |
| 2      |    2 | `temperature_centidegrees_celsius` | Signed little-endian temperature estimate in hundredths of a degree Celsius.                                                                     |
| 4      |    1 | `firmware_major`                   | Firmware semantic version major component.                                                                                                       |
| 5      |    1 | `firmware_minor`                   | Firmware semantic version minor component.                                                                                                       |
| 6      |    1 | `firmware_patch`                   | Firmware semantic version patch component.                                                                                                       |
| 7      |    1 | `reserved1`                        | Always zero.                                                                                                                                     |

## Report ID 4: Board Command

Write feature report ID `4` with one payload byte:

| Value | Command                                                                                                   |
| ----: | --------------------------------------------------------------------------------------------------------- |
|   `1` | Toggle the selected board's default LED. Does nothing when the board exposes no `PICO_DEFAULT_LED_PIN`.   |
|   `2` | Reset the board through the watchdog **only if** command `3` armed a reset within the previous 2 seconds. |
|   `3` | Arm a subsequent reset (`2`) for 2 seconds.                                                               |

Unknown command values are ignored. The report has no response payload. Remote reset is disabled by default; enable it
only for a trusted lab build with `-DPICO_UART_ALLOW_HID_RESET=1`. Enabled builds advertise that capability in
board-status `reserved0` bit 0. The reference host tool's `reset` command reads that flag first and refuses to send
arm/reset when it is clear.

## Report ID 5: RX Overflow Counts

Request feature report ID `5` to read cumulative dropped UART RX bytes for all six ports. This includes bytes already
retired by overflow recovery and bytes known to have been overwritten but not yet retired by the CDC drain path. Each
`uint32_t` counter saturates at `UINT32_MAX`; a saturated value means the actual cumulative drop count is at least that
large.

| Offset | Size | Field                  | Meaning                                                    |
| ------ | ---: | ---------------------- | ---------------------------------------------------------- |
| 0      |    1 | `version`              | Shared report layout version.                              |
| 1      |   24 | `rx_overflow_count[6]` | Six little-endian `uint32_t` values for CDC0 through CDC5. |

## Report ID 6: Hardware Info

Request feature report ID `6` to identify the MCU and query the current system clock. Its layout version is independent
of the shared version used by reports `1`, `3`, and `5`. All multi-byte values are little-endian.

| Offset | Size | Field             | Meaning                                           |
| ------ | ---- | ----------------- | ------------------------------------------------- |
| 0      | 1    | `version`         | Hardware-info layout version.                      |
| 1      | 1    | `mcu`             | `1` = RP2040; `2` = RP2350.                         |
| 2      | 4    | `system_clock_hz` | Current `clock_get_hz(clk_sys)` value in Hz.        |

RP2040 corresponds to the MCU used by Pico, and RP2350 to Pico 2. This identifies the silicon, not the board's vendor
or product model. The clock is the SDK-reported configured system frequency, read on every query; it reflects clock
overrides rather than assuming rated 125/150 MHz defaults. It is not an independently calibrated oscillator measurement.
Hosts retain the raw MCU ID and display unrecognized values as `Unknown MCU (ID)` without discarding the clock.
Unsupported layout versions, malformed sizes, and zero frequency remain errors. New MCU IDs can be added without
changing this payload layout; ported firmware must explicitly assign the appropriate ID rather than assuming RP2040.

## Report ID 7: Device Identity

Feature report `7` provides the identity independently of browser USB APIs. All text is ASCII.

| Offset | Size | Field           | Meaning                                                  |
| ------ | ---: | --------------- | -------------------------------------------------------- |
| 0      | 1    | `version`       | Identity layout version, `1`.                            |
| 1      | 32   | `product_name`  | USB product string, NUL-terminated and zero-padded.       |
| 33     | 16   | `serial_number` | Sixteen hexadecimal ASCII characters, no terminator.     |

The dashboards label this **Serial / UID**. It is the same SDK unique board ID used for the USB serial descriptor,
not report `6`'s numeric MCU-type identifier. On RP2040 the SDK derives this board identity from the external QSPI
flash unique ID; do not describe it as an RP2040 silicon serial. The SDK supplies the platform-specific identity on
RP2350. No host-generated serial or browser-session identifier is substituted.

## Report ID 8: Runtime Status

Feature report `8` provides live device counters. All multi-byte fields are little-endian.

| Offset | Size | Field                    | Meaning                                                  |
| ------ | ---: | ------------------------ | -------------------------------------------------------- |
| 0      | 1    | `version`                | Runtime layout version, `2`.                             |
| 1      | 8    | `uptime_ms`              | SDK monotonic milliseconds since boot.                   |
| 9      | 2    | `core0_service_load_basis_points` | Core 0 service load; `10000` = 100%, `65535` unknown. |
| 11     | 2    | `core1_service_load_basis_points` | Core 1 service load; same scale and sentinel.        |
| 13     | 4    | `ram_reserved_bytes`     | Static and reserved SRAM, not live allocation/stack use.  |
| 17     | 4    | `ram_total_bytes`        | Total SRAM for the target MCU.                           |

Service load is the elapsed wall time of service passes that handle work, divided by the actual measurement-window
duration, in basis points (one basis point is 0.01%). Each core publishes after at least one second; before its first
completed window it reports `65535`. Empty polling passes are excluded. Core 0 counts USB events, CDC transfers,
control progress and HID status publication; core 1 counts UART data progress, control handling and error recovery.
Waiting for a DMA transfer alone does not count as work.

The complete marked pass is charged once, including its polling overhead and any interrupts during that pass.
This is a workload proxy, not instruction-level profiling, total CPU utilization, UART line occupancy, or calibrated
remaining throughput. Both cores still busy-poll; no sleep or transport scheduling policy is changed. Monitoring traffic
and noisy RX/error handling can produce nonzero load without application traffic.

Runtime version `1` has the same byte layout but reports the previous fixed active-CPU values, not service load.
Updated hosts accept its uptime and RAM fields but show per-core load as unavailable. The shared host JSON retains
the historical `cpu_usage_basis_points` key for compatibility; only version `2` supplies measured values through it.

Hosts reject load values above `10000` except the unavailable sentinel, zero total RAM, and reservations exceeding total.
Uptime is device uptime, not time since the monitoring page connected. RAM is displayed as reserved / total KiB and a
percentage, never as a heap/stack high-water mark.

The reservation is linked SRAM from `SRAM_BASE` through `__end__`, plus scratch X/Y linked sections and both SDK stack
reservations. It excludes the remaining heap capacity and does not measure live heap allocations or stack occupancy.
Total SRAM is `SRAM_END - SRAM_BASE`, including scratch banks. These values come from the selected target/linker layout,
not a board-name lookup in the host.

## Report ID 9: Board LED State

| Offset | Size | Field     | Meaning                                                         |
| ------ | ---: | --------- | --------------------------------------------------------------- |
| 0      | 1    | `version` | LED layout version, `1`.                                        |
| 1      | 1    | `flags`   | Bit 0: LED available; bit 1: manual on; bit 2: physical output on. |

Other flag bits are reserved and zero. An unavailable LED reports zero flags; hosts show unknown states, not Off.
The physical GPIO output is the OR of the persistent manual state and transient CDC activity. Toggling report `4`'s
existing LED command changes only the manual state, so USB activity may keep the output lit after toggling manual Off.
Manual On with physical Off is invalid. All LED access occurs on core 0.

Dashboards distinguish the manual toggle action from physical output state; see
[dashboard controls](../../development/host-tools.md#pythonnet-web-ui).
The button tooltip identifies the manual state. Successful toggles reread state instead of predicting the result.
Older firmware can still accept the toggle command without report `9`; hosts retain the button, show `--` for state,
and surface the unsupported read. Boards explicitly reporting no LED have the toggle disabled.
The report sequence remains internal to gap detection and is not displayed.

## Host Tool

Host command and client behavior are documented in the [Python host guide](../../../host/python/README.md) and
[.NET host guide](../../../host/dotnet/README.md).

## Compatibility

Hosts must validate `signature0` and `version` before decoding a status report. Treat unknown report IDs, newer
versions, and unknown reserved bits as unsupported rather than attempting to infer behavior. Board-status `reserved0`
bit 0 indicates that HID reset is enabled; default firmware sends `0`. Older host tools that
rejected any nonzero `reserved0` will fail board-status reads only against reset-enabled lab builds.

Report `6` is additive: older hosts can continue reading reports `1`, `3`, and `5` from new firmware. Firmware predating
report `6` cannot supply hardware information; clients should handle that absence without discarding existing reports.
See the host guides for client-specific presentation and error behavior.

Reports `7` and `8` are additive too: existing report layouts and the six CDC interfaces are unchanged. Firmware
predating them cannot supply these values. Hosts retain legacy telemetry, show unavailable fields, and surface the
failed identity/runtime reads rather than estimating uptime or inventing a serial or utilization.

For a valid report containing an unrecognized MCU ID, hosts preserve the raw ID and system clock and present the MCU as
unknown. This does not assign meaning to future IDs; firmware must define each mapping when adding MCU support.

The source of truth for the implementation is [usb_hid.c](../../../firmware/src/usb/usb_hid.c) and the report descriptor in
[usb_descriptors.c](../../../firmware/src/usb/usb_descriptors.c).

# HID Report Reference

PicoUart exposes one vendor-defined USB HID interface named `Status Monitor`. It provides diagnostics for the six UART
bridges plus narrowly scoped board controls. UART data and line configuration remain on the matching USB CDC (`ttyACM`)
interface. All UART receivers start at board boot and bridge data whenever their matching CDC interface is connected.
DTR state is reported for monitoring only.

The HID interface uses vendor usage page `0xFF00`, vendor usage `0x01`, no boot protocol, and a 63-byte status report
alongside compact diagnostic and command feature reports. The device is identified as USB `cafe:4010` (the project's
unallocated lab identity, permitted for published project artifacts) and has one HID interface after the twelve CDC
control/data interfaces. The USB product string is `PicoUart CDC+HID PIO 8N1`. CDC interface strings advertise backend
limits: `CDC0 HW` / `CDC1 HW` and `CDC2`-`CDC5 PIO 8N1`.

## Ownership

| Operation                                                     | USB interface              | Current behavior                                                                                                                                                                                                                                                             |
| ------------------------------------------------------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| UART data                                                     | CDC0 through CDC5          | Transfers bytes to and from the matching UART.                                                                                                                                                                                                                               |
| Baud, data bits, parity, stop bits                            | CDC line-coding request    | Parsed from `SET_LINE_CODING` and queued to the matching UART backend. TinyUSB accepts the USB transfer before firmware validation; rejected or unsupported requests set health bit 2 (`control_error`) instead of stalling the CDC control pipe. PIO ports accept 8N1 only. |
| Health, traffic, ring peak, temperature, and firmware version | HID                        | Read-only monitoring data.                                                                                                                                                                                                                                                   |
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

Report ID bytes are managed by the HID transport and are not included in the payload layouts below. Status is 63 bytes
so Report ID + payload fit in one full-speed interrupt packet (64 bytes). The device attempts to publish report ID `1`
every 100 ms while its HID IN endpoint is ready. Reports are not queued when the endpoint is busy.

## Report ID 1: Status

All multi-byte values are little-endian. Channels use logical UART port IDs, so channel `0` is CDC0/UART0 and channel
`5` is CDC5/UART5.

| Offset | Size | Field        | Meaning                                                     |
| ------ | ---: | ------------ | ----------------------------------------------------------- |
| 0      |    1 | `signature0` | ASCII `P` (`0x50`).                                         |
| 1      |    1 | `version`    | Report layout version, currently `15`.                      |
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

Hosts typically treat CDC `SET_LINE_CODING` as fire-and-forget. PicoUart cannot STALL that transfer after TinyUSB has
already accepted it, so firmware surfaces rejects through HID and advertises PIO 8N1 in the USB product string and CDC
interface strings:

1. Watch health bit 3 (`control_pending`) while the worker applies a change, and while CDC soft-pending waits for the
   worker mailbox (up to 1 s from the first arm; an identical `SET_LINE_CODING` retry keeps that original deadline, but
   a distinct replacement request refreshes it and starts a new 1 s window). Invalid follow-up line-coding requests set
   `control_error` without canceling an in-flight pending apply; that older apply may finish, but cannot clear the newer
   reject.
2. Watch health bit 2 (`control_error`) after a parse failure, PIO non-8N1 reject, deferred-apply timeout (1 s), or CDC
   soft-pending mailbox timeout (1 s).
3. Use `python3 -m pico_uart monitor` - the tool decodes those bits into `control_pending` / `control_error` labels.

PIO UART ports remain 8N1-only. Hardware UART0/UART1 accept supported baud/data/parity/stop combinations within firmware
bounds (50-3 000 000 baud, 5-8 data bits, 1/2 stop, none/odd/even parity).

## Report ID 3: Board Status

Request feature report ID `3` to read the internal RP2 temperature-sensor estimate and the firmware semantic version.
Tag `v1.2.3` builds advertise `1.2.3` here. The USB device descriptor `bcdDevice` carries only major.minor as BCD (so
`1.2.3` -> `0x0102`, commonly shown as `1.02` / `1.2`).

| Offset | Size | Field                              | Meaning                                                                                                                                          |
| ------ | ---: | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| 0      |    1 | `version`                          | Report layout version, currently `15`.                                                                                                           |
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
| 0      |    1 | `version`              | Report layout version, currently `15`.                     |
| 1      |   24 | `rx_overflow_count[6]` | Six little-endian `uint32_t` values for CDC0 through CDC5. |

## Report ID 6: Hardware Info

Request feature report ID `6` to identify the MCU and query the current system clock. Its independent layout version
is `1`; the existing v15 reports are unchanged. All multi-byte values are little-endian.

| Offset | Size | Field             | Meaning                                           |
| ------ | ---- | ----------------- | ------------------------------------------------- |
| 0      | 1    | `version`         | Hardware-info layout version, currently `1`.       |
| 1      | 1    | `mcu`             | `1` = RP2040; `2` = RP2350.                         |
| 2      | 4    | `system_clock_hz` | Current `clock_get_hz(clk_sys)` value in Hz.        |

RP2040 corresponds to the MCU used by Pico, and RP2350 to Pico 2. This identifies the silicon, not the board's vendor
or product model. The clock is the SDK-reported configured system frequency, read on every query; it reflects clock
overrides rather than assuming rated 125/150 MHz defaults. It is not an independently calibrated oscillator measurement.
Hosts retain the raw MCU ID and display unrecognized values as `Unknown MCU (ID)` without discarding the clock.
Unsupported layout versions, malformed sizes, and zero frequency remain errors. New MCU IDs can be added without
changing this payload layout; ported firmware must explicitly assign the appropriate ID rather than assuming RP2040.

## Host Tool

The reference client at [host/python](../../../host/python) discovers this vendor HID collection and offers `monitor`,
`status`, `temperature`, `version`, `overruns`, `hardware`, `toggle-led`, and `reset` through `python -m pico_uart`.
Install its `hidapi` dependency before use.

`pico-uart hardware` and the [.NET host](../../../host/dotnet/README.md)'s `hardware` command return
`{"mcu":"RP2040","mcu_id":1,"system_clock_hz":125000000}`-shaped JSON. The client APIs expose `read_hardware_info()` and
`ReadHardwareInfo()`. The shared dashboard shows MCU and system clock in MHz, refreshing alongside board metadata.

## Compatibility

Hosts must validate `signature0` and `version` before decoding a status report. Treat unknown report IDs, newer
versions, and unknown reserved bits as unsupported rather than attempting to infer behavior. Board-status `reserved0`
bit 0 is a defined v15 capability flag (HID reset compiled in); default firmware still sends `0`. Older host tools that
rejected any nonzero `reserved0` will fail board-status reads only against reset-enabled lab builds.

Report `6` is additive: older hosts can continue reading reports `1`, `3`, and `5` from new firmware. Firmware predating
report `6` cannot supply hardware info; updated dashboards keep existing board metadata and telemetry while showing
unknown MCU/clock and a hardware-information error. The explicit `hardware` CLI command fails rather than guessing.

For a valid version-1 report containing an unrecognized MCU ID, both hosts preserve `mcu_id` and `system_clock_hz`.
For example, ID `3` at 200 MHz decodes to `{"mcu":"Unknown MCU (3)","mcu_id":3,"system_clock_hz":200000000}`.
This does not imply that ID `3` identifies a particular future chip; its mapping must be defined when that MCU is added.

The source of truth for the implementation is [usb_hid.c](../../../firmware/src/usb/usb_hid.c) and the report descriptor in
[usb_descriptors.c](../../../firmware/src/usb/usb_descriptors.c).

# HID Monitor

PicoUart exposes one vendor-defined USB HID interface named `Status Monitor`.
It provides diagnostics for the six UART bridges plus narrowly scoped board
controls. UART data and line configuration remain on the matching USB CDC
(`ttyACM`) interface. All UART receivers start at board boot and bridge data
whenever their matching CDC interface is connected. DTR state is reported for
monitoring only.

The HID interface uses vendor usage page `0xFF00`, vendor usage `0x01`, no boot
protocol, and a 63-byte status report alongside compact board-status and
command feature reports. The device is identified as USB
`cafe:4010` and has one HID interface after the twelve CDC control/data interfaces.

## Ownership

| Operation | USB interface | Current behavior |
| --- | --- | --- |
| UART data | CDC0 through CDC5 | Transfers bytes to and from the matching UART. |
| Baud, data bits, parity, stop bits | CDC line-coding request | Parsed from `SET_LINE_CODING` and queued to the matching UART backend. TinyUSB accepts the USB transfer before firmware validation; rejected or unsupported requests set health bit 2 (`control_error`) instead of stalling the CDC control pipe. PIO ports accept 8N1 only. |
| Health, traffic, ring peak, temperature, and firmware version | HID | Read-only monitoring data. |
| Toggle default board LED | HID command feature report | Toggles `PICO_DEFAULT_LED_PIN` when the selected board defines one. |
| Reset board | HID command feature report | Disabled by default; trusted lab builds may enable arm (`3`) then reset (`2`) within 2 seconds with `PICO_UART_ALLOW_HID_RESET=1`. |

HID must not be used to select a UART, set baud rate, change GPIO mapping, or
alter ring-buffer behavior. The three command values are board-scoped only.

## Report IDs

| Report ID | Type | Direction | Payload | Purpose |
| --- | --- | --- | --- | --- |
| `1` | Input | Device to host | 63 bytes | Periodic compact status report. |
| `3` | Feature | Host reads from device | 8 bytes | Temperature estimate and firmware semantic version. |
| `4` | Feature | Host writes to device | 1 byte | Board-control command. |

Report ID bytes are managed by the HID transport and are not included in the
payload layouts below. Status is 63 bytes so Report ID + payload fit in one
full-speed interrupt packet (64 bytes). The device attempts to publish report
ID `1` every 100 ms while its HID IN endpoint is ready. Reports are not queued
when the endpoint is busy.

## Report ID 1: Status

All multi-byte values are little-endian. Channels use logical UART port IDs, so
channel `0` is CDC0/UART0 and channel `5` is CDC5/UART5.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| 0 | 1 | `signature0` | ASCII `P` (`0x50`). |
| 1 | 1 | `version` | Report layout version, currently `15`. |
| 2 | 1 | `sequence` | Increments after each successfully published status report. |
| 3 | 60 | `channel[6]` | Six consecutive 10-byte CDC/UART channel snapshots. |

Each `channel` record has the following layout:

| Relative offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| 0 | 1 | `health` | UART status flags plus CDC-open and PIO-backend flags. |
| 1 | 1 | `ring_high_watermark_blocks` | Largest RX or TX ring occupancy, rounded up to 16-byte blocks. |
| 2 | 2 | `controller_tx_bytes` | UART controller TX byte delta. |
| 4 | 2 | `controller_rx_bytes` | UART controller RX byte delta. |
| 6 | 2 | `cdc_tx_bytes` | Device-to-host CDC byte delta. |
| 8 | 2 | `cdc_rx_bytes` | Host-to-device CDC byte delta. |

The byte counters are changes since the preceding successfully published status
report. They saturate at `65535`; a saturated value means the actual delta was
at least that large. The ring peak is cumulative from boot and saturates at
`4080` bytes.

### `health` Bits

| Bit | Meaning |
| ---: | --- |
| 0 | UART backend is initialized and ready. |
| 1 | Backend initialization failed. |
| 2 | The most recent control request failed (invalid or unsupported CDC line coding, deferred-apply timeout, or backend reject). USB `SET_LINE_CODING` may still have completed successfully. |
| 3 | A line-coding control request is pending. |
| 4 | Host has opened the matching CDC interface (DTR asserted). |
| 5 | The matching UART uses PIO; clear for hardware UART. |
| 6 | UART RX data has been overwritten since boot; drain the CDC interface or apply flow control. |
| 7 | The UART has observed a receive-status / framing error since boot (hardware UART RSR, or PIO stop-bit framing fail). |

## Line-coding rejects

Hosts typically treat CDC `SET_LINE_CODING` as fire-and-forget. PicoUart cannot
STALL that transfer after TinyUSB has already accepted it, so firmware surfaces
rejects through HID:

1. Watch health bit 3 (`control_pending`) while the worker applies a change, and
   while CDC soft-pending waits for the worker mailbox (up to 1 s from the first
   arm; later `SET_LINE_CODING` retries do not refresh that deadline). Invalid
   follow-up line-coding requests set `control_error` without clearing an
   in-flight pending apply.
2. Watch health bit 2 (`control_error`) after a parse failure, PIO non-8N1 reject,
   deferred-apply timeout (1 s), or CDC soft-pending mailbox timeout (1 s).
3. Use `python3 host/python/src/pico_uart_hid.py monitor` — the tool decodes those
   bits into `control_pending` / `control_error` labels.

PIO UART ports remain 8N1-only. Hardware UART0/UART1 accept supported
baud/data/parity/stop combinations within firmware bounds (50–3 000 000 baud,
5–8 data bits, 1/2 stop, none/odd/even parity).

## Report ID 3: Board Status

Request feature report ID `3` to read the internal RP2 temperature-sensor
estimate and the firmware semantic version. Tag `v1.2.3` builds advertise
`1.2.3` here. The USB device descriptor `bcdDevice` carries only major.minor
as BCD (so `1.2.3` → `0x0102`, commonly shown as `1.02` / `1.2`).

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| 0 | 1 | `version` | Report layout version, currently `15`. |
| 1 | 1 | `reserved0` | Always zero; reserved for board-status flags. |
| 2 | 2 | `temperature_centidegrees_celsius` | Signed little-endian temperature estimate in hundredths of a degree Celsius. |
| 4 | 1 | `firmware_major` | Firmware semantic version major component. |
| 5 | 1 | `firmware_minor` | Firmware semantic version minor component. |
| 6 | 1 | `firmware_patch` | Firmware semantic version patch component. |
| 7 | 1 | `reserved1` | Always zero. |

## Report ID 4: Board Command

Write feature report ID `4` with one payload byte:

| Value | Command |
| ---: | --- |
| `1` | Toggle the selected board's default LED. Does nothing when the board exposes no `PICO_DEFAULT_LED_PIN`. |
| `2` | Reset the board through the watchdog **only if** command `3` armed a reset within the previous 2 seconds. |
| `3` | Arm a subsequent reset (`2`) for 2 seconds. |

Unknown command values are ignored. The report has no response payload. Remote
reset is disabled by default; enable it only for a trusted lab build with
`-DPICO_UART_ALLOW_HID_RESET=1`. The reference host tool's `reset` command
sends `3` then `2` when reset support is enabled.

## Host Tool

The reference client at [host/python](../host/python) (`src/pico_uart_hid.py`)
discovers this vendor HID collection and offers `monitor`, `temperature`,
`version`, `toggle-led`, and `reset` commands. Install its `hidapi` dependency
before use.

## Compatibility

Hosts must validate `signature0` and `version` before decoding a status report.
Treat unknown report IDs, newer versions, and reserved bits as unsupported rather
than attempting to infer behavior.

The source of truth for the implementation is [usb_hid.c](../firmware/src/usb/usb_hid.c)
and the report descriptor in [usb_descriptors.c](../firmware/src/usb/usb_descriptors.c).
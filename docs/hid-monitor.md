# HID Monitor

PicoUart exposes one vendor-defined USB HID interface named `Status Monitor`.
It provides diagnostics for the six UART bridges plus narrowly scoped board
controls. UART data and line configuration remain on the matching USB CDC
(`ttyACM`) interface.

The HID interface uses vendor usage page `0xFF00`, vendor usage `0x01`, no boot
protocol, and 64-byte status and PIO-statistics reports alongside compact
board-status and command feature reports. The device is identified as USB
`cafe:4010` and has one HID interface after the twelve CDC control/data interfaces.

## Ownership

| Operation | USB interface | Current behavior |
| --- | --- | --- |
| UART data | CDC0 through CDC5 | Transfers bytes to and from the matching UART. |
| Baud, data bits, parity, stop bits | CDC line-coding request | Parsed from `SET_LINE_CODING` and queued to the matching UART backend. |
| Status, PIO counters, and temperature | HID | Read-only monitoring data. |
| Toggle default board LED | HID command feature report | Toggles `PICO_DEFAULT_LED_PIN` when the selected board defines one. |
| Reset board | HID command feature report | Immediately reboots through the watchdog. |

HID must not be used to select a UART, set baud rate, change GPIO mapping, or
alter ring-buffer behavior. The two command values are board-scoped only.

## Report IDs

| Report ID | Type | Direction | Payload | Purpose |
| --- | --- | --- | --- | --- |
| `1` | Input | Device to host | 64 bytes | Periodic compact status report. |
| `2` | Feature | Host reads from device | 64 bytes | Full-width PIO UART counters. |
| `3` | Feature | Host reads from device | 4 bytes | Internal temperature sensor estimate. |
| `4` | Feature | Host writes to device | 1 byte | Board-control command. |

Report ID bytes are managed by the HID transport and are not included in the
64-byte payload layouts below. The device attempts to publish report ID `1`
every 100 ms while its HID IN endpoint is ready. Reports are not queued when
the endpoint is busy.

## Report ID 1: Status

All multi-byte values are little-endian. Array indexes are logical UART port
IDs, so index `0` is CDC0/UART0 and index `5` is CDC5/UART5.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| 0 | 1 | `signature0` | ASCII `P` (`0x50`). |
| 1 | 1 | `signature1` | ASCII `U` (`0x55`). |
| 2 | 1 | `version` | Report layout version, currently `11`. |
| 3 | 1 | `port_count` | Number of UART ports, currently `6`. |
| 4 | 1 | `sequence` | Increments after each successfully published status report. |
| 5 | 1 | `worker_state` | UART worker state bitfield. |
| 6 | 1 | `last_command_status` | Last UART control result plus HardFault-core marker. |
| 7 | 1 | `last_command_port` | Temporary worker heartbeat diagnostic value. |
| 8 | 6 | `backend[6]` | `0` for hardware UART; `1` for PIO UART. |
| 14 | 6 | `tx_pin[6]` | Configured TX GPIO number. |
| 20 | 6 | `rx_pin[6]` | Configured RX GPIO number. |
| 26 | 6 | `status[6]` | Per-port status flag bitfield. |
| 32 | 8 | `pio_rx_framing_error_count[4]` | Saturated 16-bit deltas for UART2 through UART5. |
| 40 | 8 | `pio_tx_dma_claim_failure_count[4]` | Saturated 16-bit deltas for UART2 through UART5. |
| 48 | 8 | `pio_tx_polled_bytes[4]` | Saturated 16-bit deltas for UART2 through UART5. |
| 56 | 8 | `pio_tx_dma_bytes[4]` | Saturated 16-bit deltas for UART2 through UART5. |

The PIO counter arrays use index `0` for UART2, `1` for UART3, `2` for UART4,
and `3` for UART5. They represent changes since the preceding successfully
published status report; a value of `65535` means the actual delta was at least
that large.

### `worker_state` Bits

| Bits | Meaning |
| --- | --- |
| 0 | UART worker has been launched. |
| 1 | Current transport scope is 8N1-only. |
| 2 through 4 | UART port currently being polled by the worker; `6` means between poll passes. |
| 5 through 7 | Low three bits of the worker heartbeat. |

### `last_command_status` Bits

Bits 0 through 3 carry the latest UART-driver command status:

| Value | Meaning |
| ---: | --- |
| 0 | Completed successfully. |
| 1 | Worker not started. |
| 2 | Invalid port. |
| 3 | Invalid argument. |
| 4 | One or more backend initializations failed. |
| 5 | Backend rejected the request. |
| 6 | Unsupported request. |
| 7 | Accepted and queued for deferred application. |
| 8 | Worker command timed out. |

Bits 4 through 5 identify the core that last entered HardFault: `0` means no
fault recorded, `1` means core 0, and `2` means core 1.

### Per-Port `status` Bits

| Bit | Meaning |
| ---: | --- |
| 0 | UART backend is initialized and ready. |
| 1 | Backend initialization failed. |
| 2 | The most recent control request failed. |
| 3 | A line-coding control request is pending. |

## Report ID 2: PIO Counter Snapshot

Request feature report ID `2` to read the full 32-bit, little-endian counters.
This report has no signature or version field; its layout is exactly four
consecutive arrays, each indexed as UART2, UART3, UART4, UART5.

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 16 | `rx_framing_error_count[4]` |
| 16 | 16 | `tx_dma_claim_failure_count[4]` |
| 32 | 16 | `tx_polled_bytes[4]` |
| 48 | 16 | `tx_dma_bytes[4]` |

These counters are cumulative since firmware boot. They are intended for
monitoring and test diagnostics; they do not configure or control UART traffic.

## Report ID 3: Board Status

Request feature report ID `3` to read the internal RP2 temperature-sensor
estimate.

| Offset | Size | Field | Meaning |
| --- | ---: | --- | --- |
| 0 | 1 | `version` | Report layout version, currently `11`. |
| 1 | 1 | `reserved` | Always zero; reserved for board-status flags. |
| 2 | 2 | `temperature_centidegrees_celsius` | Signed little-endian temperature estimate in hundredths of a degree Celsius. |

## Report ID 4: Board Command

Write feature report ID `4` with one payload byte:

| Value | Command |
| ---: | --- |
| `1` | Toggle the selected board's default LED. Does nothing when the board exposes no `PICO_DEFAULT_LED_PIN`. |
| `2` | Immediately reset the board through the watchdog. The USB device disconnects and enumerates again after startup. |

Unknown command values are ignored. The report has no response payload.

## Host Tool

The reference client at [host/python](../host/python) discovers this vendor HID
collection and offers `monitor`, `temperature`, `statistics`, `toggle-led`, and
`reset` commands. Install its `hidapi` dependency before use.

## Compatibility

Hosts must validate `signature0`, `signature1`, `version`, and `port_count`
before decoding a status report. Treat unknown report IDs, newer versions, and
reserved bits as unsupported rather than attempting to infer behavior.

The source of truth for the implementation is [usb_hid.c](../firmware/src/usb/usb_hid.c)
and the report descriptor in [usb_descriptors.c](../firmware/src/usb/usb_descriptors.c).
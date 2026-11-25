# HID Report Reference

This is the wire specification for PicoUart's HID interface: board identity, configuration, telemetry, board actions,
and per-channel monitoring. The [architecture](../architecture.md#usb-interfaces) describes its role alongside CDC.

## Interface Basics

- One vendor-defined HID interface, `Status Monitor`, follows the six CDC ports (twelve USB interfaces). It uses usage
  page `0xFF00`, usage `0x01`, and no boot protocol.
- The device is USB `2e8a:116b`, allocated to PicoUart by the Raspberry Pi Foundation
  ([allocation list](https://github.com/raspberrypi/usb-pid)). The manufacturer string is `djwinter`, the product
  `PicoUart`. CDC interface strings advertise backend limits: `CDC0 HW`, `CDC1 HW`, and `CDC2`-`CDC5 PIO 8N1`.

**Wire rules:** multi-byte integers are little-endian. Offsets and sizes exclude the report-ID byte. Every report fits
in 64 bytes including the ID, with no padding. Layout version `1` is independent of the firmware version;
[Discovery](#discovery) explains layout selection.

## Report ID Allocation

| Report ID | Name | Type | Direction | Payload | Purpose |
| --------- | ---- | ---- | --------- | ------- | ------- |
| `1` | [Device Identity and Capabilities](#report-id-1-device-identity-and-capabilities) | Feature | Host reads | 27 bytes | Identity, capabilities, total/reserved RAM. |
| `2` | [Board Configuration](#report-id-2-board-configuration) | Feature | Host reads | 36 bytes | Stored profile, pins, reset cause, buffer capacities. |
| `3` | [Board Telemetry](#report-id-3-board-telemetry) | Feature | Host reads | 20 bytes | Temperature, LED, uptime, service load, running clock. |
| `10`-`15` | [Channel Snapshots (CDC0–CDC5)](#reports-10-15-channel-snapshots) | Input / Feature | Device publishes / host reads | 30 bytes | State, measurement validity, applied settings, traffic. |
| `20` | [Board Action and Result](#report-id-20-board-action-and-result) | Feature | Host reads/writes | 4 bytes | LED/reboot/save actions and results. |
| `30`-`35` | [Channel Diagnostics (CDC0–CDC5)](#reports-30-35-channel-diagnostics) | Feature | Host reads | 24 bytes | RX fault history, last configuration error, buffer peaks. |

All sixteen report IDs are always present, and ID `0` is unused. Capability flags identify features unavailable on a
board. Writes to read-only reports are rejected without side effects.

For read cadence and validation order, see [Host Rules](#host-rules).

## Report ID 1: Device Identity and Capabilities

Read once per connection. The contents are static until reboot.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version `1`. |
| 1 | 2 | `product_id` | Project-owned product-family identifier; independent of USB VID/PID. |
| 3 | 8 | `device_uid` | Opaque SDK board identity bytes, in USB serial-string order. |
| 11 | 1 | `firmware_major` | Firmware semantic version major component. |
| 12 | 1 | `firmware_minor` | Firmware semantic version minor component. |
| 13 | 1 | `firmware_patch` | Firmware semantic version patch component. |
| 14 | 1 | `mcu` | Silicon family: `1` RP2040; `2` RP2350. |
| 15 | 1 | `board_model` | Configured firmware build target, defined below. |
| 16 | 1 | `hardware_channel_mask` | Within report 2's channel count, `1` means hardware UART and `0` means PIO UART. |
| 17 | 1 | `capabilities` | Static board-control capability flags. |
| 18 | 1 | `clock_profile_mask` | Firmware-accepted boot profile request bits; see report 2. |
| 19 | 4 | `ram_total_bytes` | Total SRAM for the MCU/linker layout. |
| 23 | 4 | `ram_reserved_bytes` | Static and reserved SRAM for this firmware build. |
| **Total** | **27** | | **28 bytes including the report ID.** |

### Discovery

Every product using this convention shares a three-byte prefix: the layout version at offset 0 and the
little-endian product ID at offsets 1-2. Read the descriptor-declared length, check the prefix, choose the matching
product contract, then validate the full report. Decode each product only with a supported layout version, and
reject unknown products rather than decoding them with a guessed layout.

| Product ID | Meaning |
| ---------: | ------- |
| `0x0000` | Reserved / unspecified. |
| `0x0001` | PicoUart, across supported board/MCU variants. |
| `0x0002`-`0xfffe` | Assign as other project products are introduced. |
| `0xffff` | Reserved. |

### Device Identity Fields

**UID:** an ordered byte sequence. Uppercase hexadecimal with two characters per byte matches the 16-character USB
serial. On RP2040 it is the QSPI flash UID; on RP2350 it is the SDK platform identity.

**Firmware version:** a `v1.2.3` tag reports `1.2.3`; USB `bcdDevice` carries only major.minor. Use the version for
diagnostics only; capability flags gate operations.

The board model is the configured build target; firmware does not detect hardware. Keep unknown MCU and board numbers
as reported rather than inventing names.

| Board model | Board | MCU | Support status |
| ----------: | ----- | --- | -------------- |
| `0` | Unknown / unspecified | Reported separately | No specific board identity claimed. |
| `1` | Pico | RP2040 | Existing firmware target. |
| `2` | Pico W | RP2040 | Reserved; not a supported build target. Pico W boards run `pico` and report `1`. |
| `3` | Pico 2 | RP2350 | Existing firmware target. |
| `4` | Pico 2 W | RP2350 | Reserved; not a supported build target. Pico 2 W boards run `pico2` and report `3`. |

### Backend and Capability Fields

The hardware channel mask uses report 2's channel count (six for PicoUart). Reject bits set above that count.
PicoUart reports `0x03`, so CDC0 and CDC1 use hardware UARTs and the rest use PIO.

| Capability bit | Meaning |
| -------------: | ------- |
| 0 | Board identification LED available. |
| 1 | Confirmed standalone reboot enabled by `PICO_UART_ALLOW_HID_RESET`. |
| 2 | Boot-configuration save-and-reboot available. |
| 3-7 | Reserved, zero. |

Clock-profile mask bits 0-4 match the profile IDs in report 2. Bit 0 is always set, and bits 5-7 are zero. A set bit
means the firmware accepts the request; board stability depends on the individual board and test conditions.

### Firmware RAM Reservation

`ram_reserved_bytes` is build-time accounting: linked SRAM through `__end__`, scratch sections, and both stack
reservations. It covers the firmware's reserved memory; free heap and runtime memory pressure are outside this value.
Reject a zero total or a reservation larger than the total.

## Report ID 2: Board Configuration

Read-only. Read it once per connection and again after a reboot. It describes the boot-time configuration, the fixed
hardware map, and the reset that started this boot. Saving a new profile does not change it until the board reboots.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version `1`. |
| 1 | 1 | `stored_profile` | One-byte profile ID accepted from storage at boot; `255` unavailable. |
| 2 | 1 | `channel_count` | Logical UART/CDC channel count; `6` for PicoUart. |
| 3 | 4 | `channel_0_pins` | CDC0 GPIOs in TX, RX, RTS, CTS order. |
| 7 | 4 | `channel_1_pins` | CDC1 GPIOs in TX, RX, RTS, CTS order. |
| 11 | 4 | `channel_2_pins` | CDC2 GPIOs in TX, RX, RTS, CTS order. |
| 15 | 4 | `channel_3_pins` | CDC3 GPIOs in TX, RX, RTS, CTS order. |
| 19 | 4 | `channel_4_pins` | CDC4 GPIOs in TX, RX, RTS, CTS order. |
| 23 | 4 | `channel_5_pins` | CDC5 GPIOs in TX, RX, RTS, CTS order. |
| 27 | 1 | `reset_cause` | Cause of the reset that began this boot; `0` if unknown. |
| 28 | 2 | `uart_tx_ring_capacity_bytes` | Usable per-channel UART TX-ring capacity. |
| 30 | 2 | `uart_rx_ring_capacity_bytes` | Usable per-channel UART RX-ring capacity. |
| 32 | 2 | `cdc_tx_fifo_capacity_bytes` | Per-channel device-to-host CDC FIFO capacity. |
| 34 | 2 | `cdc_rx_fifo_capacity_bytes` | Per-channel host-to-device CDC FIFO capacity. |
| **Total** | **36** | | **37 bytes including the report ID.** |

### Channel Hardware Map and Capacities

Each entry is a GPIO number (not a header pin), and `255` means unassigned. Entries are in channel order, from the
firmware's board configuration. An assigned RTS/CTS pin does not mean flow control is running.

All four capacities are nonzero and the same for every channel. They describe the storage behind the matching peaks in
reports 30-35; USB in-flight bytes are separate.

DTR is a CDC session signal and is not part of this map. If report 2 cannot be read, show the pins and channel count as
unknown; do not infer GPIO 0.

### Reset Cause

The firmware reports the most specific cause supported by hardware flags or firmware markers. If the platform cannot
distinguish causes, it reports Unknown; RP2040 combines power-on and brownout into one hardware indication.

| Value | Meaning |
| ----: | ------- |
| `0` | Unknown or indistinguishable. |
| `1` | Power-on. |
| `2` | Watchdog reset without a software-requested marker. |
| `3` | Software-requested reset. |
| `4` | Watchdog recovery after firmware recorded a HardFault in watchdog scratch storage. |
| `5` | Power-on or brownout (combined RP2040 indication). |
| `6` | Brownout. |
| `7` | External RUN-pin reset. |
| `8` | Debugger reset. |
| `9` | User-requested standalone board reboot (firmware marker). |
| `10` | Configuration-save reboot after successful flash verification (firmware marker). |
| `11`-`255` | Unassigned; hosts preserve the numeric value and display it as unknown. |

Reboot and save markers live in watchdog scratch registers and are consumed at boot. They identify the firmware path;
the requesting host is not recorded. A recorded HardFault takes priority over those markers.

### Boot Configuration

| Profile ID | Requested clock / meaning | Targets |
| ------: | ----- | ------- |
| `0` | SDK default; no clock reconfiguration | Both |
| `1` | 150 MHz | Both |
| `2` | 200 MHz | Both |
| `3` | 250 MHz | Both |
| `4` | 280 MHz | Both |
| `5`-`254` | Unassigned; invalid in the currently specified layout | None |
| `255` | Unavailable sentinel for `stored_profile` readback; reject it as a save request | None |

`stored_profile` records the request; report 3 gives the running clock. The two values can differ, so use each for its
own purpose.

At boot the firmware picks the highest supported frequency at or below the request. The current build caps RP2040 at
250 MHz and RP2350 at 280 MHz. If no frequency is eligible, or the clock change fails, it falls back to SDK defaults.

Invalid or erased storage reads as `255` and boots with SDK defaults; flash is not rewritten. Watchdog recovery can also
fall back to defaults while keeping the stored request. Check report 3 to confirm the clock after boot.

## Report ID 3: Board Telemetry

Live measurements only.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version `1`. |
| 1 | 1 | `led_flags` | Bit 0: identification output On. Other bits zero. |
| 2 | 2 | `temperature_centidegrees_celsius` | Signed internal RP2 sensor estimate, in hundredths of a degree Celsius. |
| 4 | 8 | `uptime_ms` | SDK monotonic milliseconds since boot. |
| 12 | 2 | `core0_service_load_basis_points` | `10000` = 100%; `65535` unknown. |
| 14 | 2 | `core1_service_load_basis_points` | Same scale and sentinel. |
| 16 | 4 | `system_clock_hz` | Current SDK-reported `clock_get_hz(clk_sys)`, nonzero. |
| **Total** | **20** | | **21 bytes including the report ID.** |

If any mandatory measurement fails, the whole GET fails. The load sentinel `65535` and an absent LED are valid values.

**Service load** is the wall time spent in service passes that did real work, divided by the measurement window.
Each core publishes about once a second and reports `65535` before its first window. Core 0 counts USB, CDC, control,
and HID work; core 1 counts UART data, control, and error recovery. Idle polling and waiting on DMA are excluded. It
measures service workload; use separate measurements for CPU utilization or spare throughput. Reject values above
`10000` except the sentinel.

## Reports 10-15: Channel Snapshots

Report `10 + channel` covers CDC0–CDC5. The firmware publishes an Input report for each channel about every **500 ms**.
A Feature GET returns the same layout on demand.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version `1`. |
| 1 | 1 | `channel_state` | Current lifecycle state; previous faults are reported in diagnostics. |
| 2 | 1 | `uart_measurement_state` | Unavailable, Current, or Cached UART statistics, defined below. |
| 3 | 4 | `baud_rate` | Last successfully applied nominal baud setting; zero when unavailable. |
| 7 | 1 | `data_bits` | Applied data bits; zero when baud is unavailable. |
| 8 | 1 | `stop_bits` | Applied stop bits; zero when baud is unavailable. |
| 9 | 1 | `parity` | Applied none=`0`, odd=`1`, even=`2`; zero when unavailable. |
| 10 | 2 | `sequence` | Accepted Input publication sequence, modulo 65536. |
| 12 | 2 | `interval_ms` | Milliseconds since preceding accepted Input; `65535` saturated, `0` unusable for rates. |
| 14 | 4 | `uart_tx_bytes` | Boot-lifetime handoff counter, modulo 2^32. |
| 18 | 4 | `uart_rx_bytes` | Boot-lifetime published RX counter, including subsequently overwritten bytes, modulo 2^32. |
| 22 | 4 | `cdc_tx_bytes` | Boot-lifetime TinyUSB acceptance counter, modulo 2^32. |
| 26 | 4 | `cdc_rx_bytes` | Boot-lifetime UART TX-ring admission counter, modulo 2^32. |
| **Total** | **30** | | **31 bytes including the report ID.** |

### Channel State

| Value | State | Meaning |
| ----: | ----- | ------- |
| `0` | Closed | CDC DTR closed; backend shutdown may still be pending. |
| `1` | Starting | CDC open; initialization or initial configuration not complete. |
| `2` | Ready | CDC open, backend ready, no pending control or current failure. |
| `3` | Configuring | CDC open and backend ready; a line-coding request is pending. |
| `4` | Error | CDC open with a current initialization/control failure; traffic may still progress. |
| `5`-`255` | Unassigned | Invalid in this layout. |

When more than one state applies, the earliest in this order wins: Closed, Error, Starting, Configuring, Ready. A
missing or stale report is shown as host-side Unknown. Read the diagnostics report for an error reason.
The request lifecycle behind Configuring and Error is described in the
[control plane design](../control-plane-design.md).

### UART Measurement State

| Value | State | Meaning | Diagnostic sample age |
| ----: | ----- | ------- | --------------------- |
| `0` | Unavailable | No coherent UART statistics sample exists; UART counters are zero placeholders. | `255` |
| `1` | Current | Coherent UART statistics acquired for this capture. | `0` |
| `2` | Cached | Last accepted UART statistics reused; UART rates unavailable. | `0` after rounding, or `255` when saturated |
| `3`-`255` | Unassigned | Invalid in this layout. | Invalid |

UART statistics come from the core-1 backend through a lock-free snapshot that can fail. On failure, the report keeps
the last good sample and marks it Cached. Current describes the sample capture; a stopped port can still have a
current final sample. The CDC counters are owned by core 0 and are always fresh.

### Traffic Counters

| Direction | Counter | Counts |
| --------- | ------- | ------ |
| Host → UART | `cdc_rx_bytes` | Bytes consumed from TinyUSB into UART TX ring. |
| Host → UART | `uart_tx_bytes` | Peripheral/FIFO handoff: HW/PIO DMA completion or PIO polling FIFO writes. |
| UART → Host | `uart_rx_bytes` | DMA bytes published into RX ring, including bytes subsequently overwritten. |
| UART → Host | `cdc_tx_bytes` | Bytes accepted by TinyUSB for transmission to the host. |

These count handoffs rather than delivered bytes. Closing a port or resetting USB can discard bytes already counted.
Use the diagnostics counters and payload checks to investigate loss. The counters run from boot and wrap at 2^32, and
USB reconnects do not reset them. The applied baud rate reports the configured nominal rate.

### Snapshot Consistency

Each report is captured coherently. The baud rate, framing, and state come from one applied configuration, and the
four counters are read together at the interval boundary. Treat reports for different channels, and snapshots versus
diagnostics, as independent captures.

### Sequence and Interval

- Each channel has its own sequence and interval. A sequence number is used up only when USB accepts the Input report,
  so the first accepted report is `1`. A Feature read returns the last committed sequence, or `0` before the first.
  USB reset restarts the sequence but not the counters.
- `interval_ms` is the time since this channel's previous accepted Input, capped at `65535`. Values `0` and `65535`
  cannot produce a rate.
- When the endpoint is busy, publication is delayed and old snapshots are dropped instead of sent in a burst. Due
  channels are served round-robin.

## Report ID 20: Board Action and Result

A SET requests one action, and a GET returns the latest board-wide result. Both use four bytes.

| Offset | Size | SET_REPORT field | GET_REPORT field |
| -----: | ---: | ---------------- | ---------------- |
| 0 | 1 | `version = 1` | `version = 1` |
| 1 | 1 | `action`: action from the table below. | Latest recognizable `action`; `0` when idle or unrecognized. |
| 2 | 1 | `flags`: bit 0 confirms reboot/save; bit 1 acknowledges unsafe overclocking. Bits 2-7 zero. | `status`, defined below. |
| 3 | 1 | `profile`: profile to save for action 4; zero for other actions. | `error_reason`: action rejection or execution failure. |

Always build a fresh SET. Bytes 2-3 mean different things in each direction, so do not send a GET response as a SET.
GET requests read the latest result; they do not run or repeat actions.

| Action | Name | Required flags | Behavior |
| -----: | ---- | -------------- | -------- |
| `1` | LED Off | `0` | Set identification LED Off. |
| `2` | LED On | `0` | Set identification LED On. |
| `3` | Reboot | `0x01` | Schedule a watchdog reboot; no separate arm transaction. |
| `4` | Save profile and reboot | `0x01` for profile 0; `0x03` for non-default profiles | Validate and save the profile, then automatically reboot. |

LED actions need capability bit 0, reboot needs bit 1, and save needs bit 2. Building with
`PICO_UART_ALLOW_HID_RESET=0` removes standalone reboot but not save-and-reboot. The LED starts Off, LED actions set a
state rather than toggling it, and only core 0 drives the LED. The confirm flag records the host's intent; it is not
authentication.

### Action Results

| Status | Name | Meaning |
| -----: | ---- | ------- |
| `0` | Idle | No action result yet. |
| `1` | Pending | Accepted and still running. |
| `2` | Completed | LED action completed, or reboot scheduled; a save has passed flash verification. |
| `3` | Invalid | Malformed request, invalid profile, or missing confirmation/acknowledgement. |
| `4` | Unsupported | Unknown action or required capability unavailable. |
| `5` | Busy | Save rejected because a CDC DTR session is open. |
| `6` | Failed | Execution failed, including flash write/verification failure. |

| Error reason | Name | Required status |
| -----------: | ---- | --------------- |
| `0` | None | Idle, Pending, or Completed |
| `1` | Malformed payload: version, length, flags, or unused argument | Invalid |
| `2` | Unknown or firmware-unadvertised clock profile request | Invalid |
| `3` | Missing overclock acknowledgement | Invalid |
| `4` | Missing destructive-action confirmation | Invalid |
| `5` | Unknown action or unavailable capability | Unsupported |
| `6` | CDC DTR session open | Busy |
| `7` | Flash operation failed | Failed |
| `8` | Flash verification failed | Failed |
| `9` | Other execution failure | Failed |

Idle always carries action `0`. A rejected request reports its action if the request was well-framed with a known
action, and `0` otherwise. A result stays until the next action or a reboot. There are no request IDs, so a result does
not show which client sent the action: use a single controlling host and serialize its writes.

### Action Validation and Execution

The firmware checks a SET in this order and publishes the first failure:

1. If an action is Pending, or Completed and awaiting reboot, drop the SET without changing the current result.
2. Wrong length, version, or reserved flag bits give Invalid/1.
3. An unknown action or a missing capability gives Unsupported/5.
4. Bad action-specific fields give Invalid/1. LED actions need flags and profile zero. Reboot needs profile zero and
   flag bit 1 clear. Save of profile 0 also needs bit 1 clear.
5. For save, an unadvertised profile gives Invalid/2, then a missing confirm gives Invalid/4, then a missing overclock
   acknowledgement gives Invalid/3. Reboot also needs the confirm flag (Invalid/4).
6. An open CDC port gives Busy/6. Readiness is checked again just before running; flash errors give 7 or 8, and any
   other failure gives 9.

`Invalid/n` means the Invalid result status with reason code `n`. Rejected requests have no side effects. An accepted
SET publishes its result, either Pending or an immediate Completed, before the USB transfer finishes.

### Save and Reboot Safety

Saving writes flash and then reboots. Besides closing every CDC port, stop external UART traffic: flash erase stalls
both cores, and the reboot drops any queued bytes.

- The write runs in the USB poll loop, outside the USB callback, with core 1 locked out by the SDK flash-safe API.
  Only the final flash sector is erased, and saving the profile that is already stored skips the write.
- The record is CRC-protected and checked in full before the result reports Completed. Completed means the profile was
  verified in flash, not that the next boot succeeded.
- An interrupted write boots SDK defaults because the clock profile occupies one flash sector.

## Reports 30-35: Channel Diagnostics

Report `30 + channel` is a read-only Feature with no Input publication. Reads preserve history.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version `1`. |
| 1 | 1 | `uart_measurement_state` | Unavailable, Current, or Cached; see [UART Measurement State](#uart-measurement-state). |
| 2 | 1 | `uart_sample_age_100ms` | UART acquisition age in 100 ms units; see [UART Measurement State](#uart-measurement-state). |
| 3 | 1 | `last_control_error_reason` | Most recent configuration error, retained as diagnostic history. |
| 4 | 4 | `rx_overrun_count` | Saturating known dropped UART RX bytes. |
| 8 | 4 | `rx_error_count` | Saturating count of controller error observations. |
| 12 | 4 | `last_fault_uptime_s` | Latest observed RX fault or recorded configuration error, in saturating uptime seconds. |
| 16 | 2 | `uart_tx_ring_peak_bytes` | Boot-lifetime observed UART TX-ring peak, in bytes. |
| 18 | 2 | `uart_rx_ring_peak_bytes` | Boot-lifetime observed UART RX-ring peak, in bytes. |
| 20 | 2 | `cdc_tx_fifo_peak_bytes` | Boot-lifetime observed device-to-host FIFO peak. |
| 22 | 2 | `cdc_rx_fifo_peak_bytes` | Boot-lifetime observed host-to-device FIFO peak. |
| **Total** | **24** | | **25 bytes including the report ID.** |

### Last Configuration Error

| Value | Meaning |
| ----: | ------- |
| `0` | No configuration error recorded since firmware initialization. |
| `1` | Unsupported baud setting. |
| `2` | Unsupported framing. |
| `3` | Backend configuration failed. |
| `4` | Control operation timed out. |
| `5` | Other or unknown configuration error. |
| `6`-`255` | Unassigned. |

This field records history. It keeps its value through later successes, port close and reopen, and USB reconnects, and
clears only at boot.

### Fault History

- The two RX counters saturate rather than wrap, and firmware does not clear them. `rx_overrun_count` counts known
  dropped bytes. `rx_error_count` counts sticky error flags seen when sampled, so several bad frames can count as one;
  it is not a per-byte or per-frame total.
- `last_fault_uptime_s` is the uptime of the latest RX fault or configuration error. It is monotonic within a boot.
  A startup failure on its own does not advance it.
- "No history" means both counters, the reason, and the timestamp are zero. A reason with zero RX counts is valid.
- The USB core samples faults about every 100 ms, whether or not anyone is reading.
- Nonzero counts preserve past observations and cannot establish current health. Possible causes include a floating
  RX line, a break, a wrong baud rate, or wiring faults; the counters cannot distinguish them.
- There is no boot ID. After reconnecting, start new comparison baselines rather than claiming exact fault counts across
  the gap.

### Peak Measurements

Peaks are the largest occupancy seen at the firmware's sampling points, so a brief maximum can be missed. Compare them
with the capacities in report 2. A full peak does not by itself mean current congestion or data loss. Peaks survive
port close and reopen.

## Host Rules

### Polling and Validation

1. Read report 1, check the [discovery prefix](#discovery), and validate the full report before sending
   any command. PicoUart requires version 1 and exactly 27 bytes.
2. Read report 2 once per connection. Check that the channel count is 6 and use it to check report 1's channel mask.
3. Poll report 3 every 500-1000 ms.
4. Consume Input reports 10-15 for snapshots and traffic deltas. Feature reads give the current state on demand.
5. Read reports 30-35 on demand, or slowly if the user enables alerts.
6. Send actions only through report 20, then read report 20 until the result is final.

This cadence produces one or two telemetry reads and about twelve channel Inputs per second. After a disconnect or
reboot, discard cached reports and pending actions, then start again at step 1.

For every report, validate its type, ID, exact length, version, reserved bits, enum values, and field consistency.

### Rates

Take the counter delta modulo 2^32 and divide by the device's `interval_ms`. The first sample after a (re)connect only
sets the baseline. A sequence gap, a stale stream, or a non-Current UART sample discards that channel's rate window
while keeping its totals; other channels are unaffected. Combine windows by summing bytes and intervals rather than
averaging rates. Feature reads of any report leave sequences, rate baselines, and totals unchanged.

Wrap-safe deltas need fewer than 2^32 bytes between samples, which is hours at UART speeds. A 16-bit sequence cannot
detect a loss of exactly 65536 reports, so rebaseline after any long pause.

### Board Actions

A completed SET transfer does not prove the action ran. Read report 20 right away and then every 50 ms while it is
Pending, for up to 3000 ms. Give each SET and GET its own 3000 ms deadline. Reboot and save reset the board 1000 ms
after publishing Completed; later reads and writes neither delay nor cancel this.

A timeout or disconnect leaves the outcome unknown. Do not retry a reboot or save automatically. Reconnect, read
report 2, and let the user decide.

## Conformance Examples

Payloads omit the report-ID byte. Action examples assume the LED and save capabilities are present, CDC ports are
closed, and nothing is pending.

| Request | Response | Meaning |
| ------- | -------- | ------- |
| `01 02 00 00` (LED On) | `01 02 02 00` | Completed. |
| `01 04 01 01` (save profile 1, confirmed, no overclock ack) | `01 04 03 03` | Invalid/3; no flash write. |

| Scenario | Required behavior |
| -------- | ----------------- |
| LED request with flags `0x80` | Publish action 1 or 2 with Invalid/error 1; leave LED unchanged. |
| Unknown action `0x7f` with flags `0x01`, otherwise well-framed | Publish action 0 with Unsupported/error 5. |
| Unknown action `0x7f` with flags `0x80`, otherwise well-framed | Publish action 0 with Invalid/error 1 before action validation. |
| Reboot without confirmation | Publish action 3 with Invalid/error 4; no reset. |
| Save while any CDC DTR session is open | Publish action 4 with Busy/error 6; no flash write or reset. |
| First channel publication fails, a Feature read follows, then publication succeeds | Failed candidate uses sequence 1 without committing; Feature returns 0; first accepted Input returns 1. |
| Channel sequence `65535` followed by `0` | Normal modulo-65536 continuity, not a gap. |
| Input at time T; Feature read at T+250 ms; next Input at T+500 ms | Feature read does not commit a baseline. Next Input still covers 500 ms from T and includes all interval bytes once. |
| Channel 0 sequence jumps from 10 to 12 while channel 1 advances normally | Recover channel 0's cumulative totals, discard its rate window, and leave channel 1 unaffected. |
| Line coding changes during snapshot capture | Report one coherent applied configuration: either the old or the new one. |

## Versioning

Version `1` is used by published firmware; earlier development layouts are not compatibility targets. An incompatible
change to a report's length, units, meaning, or validation requires a version bump and a matching update to this
document. The descriptor and firmware changes require matching Python, .NET, and Direct Dashboard support. Hosts must
support a layout before decoding it.

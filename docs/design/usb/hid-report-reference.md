# HID Report Reference

This is the wire specification for PicoUart's HID interface: board identity, configuration, telemetry, board actions,
and per-channel monitoring. For what HID is for and how it relates to the CDC ports, read the
[USB interfaces](usb-interfaces.md) first.

## Interface Basics

- One vendor-defined HID interface, `Status Monitor`, follows the twelve CDC interfaces. It uses usage page `0xFF00`,
  usage `0x01`, and no boot protocol.
- The device is USB `2e8a:116b`, allocated to PicoUart by the Raspberry Pi Foundation
  ([allocation list](https://github.com/raspberrypi/usb-pid)). The manufacturer string is `djwinter`, the product
  `PicoUart`. CDC interface strings advertise backend limits: `CDC0 HW`, `CDC1 HW`, and `CDC2`-`CDC5 PIO 8N1`.
- HID never carries UART data or line coding, never selects pins, and never changes ring behavior. Reading a report
  does not change transport ownership.

**Wire rules:** multi-byte integers are little-endian, and the UID is an ordered byte sequence. Offsets and sizes
exclude the report-ID byte. Every report fits in 64 bytes including the ID. All layouts here are version `1`,
independent of the firmware version. Within the selected product, report type, ID, length, and version together
identify a layout. There is no padding.

## Report ID Allocation

The ranges are a reusable convention for Pico projects. The assignments are PicoUart's.

| IDs | Generic role | PicoUart assignments |
| --- | ---- | ----------- |
| `1`-`9` | Device information and monitoring | `1` Identity, `2` Board Configuration, `3` Telemetry; `4`-`9` unassigned. |
| `10`-`19` | Application monitoring | Six Channel Snapshots, addressed as `10 + channel`; `16`-`19` unassigned. |
| `20`-`29` | Device and application control | `20` Board Action and Result; `21`-`29` unassigned. |
| `30`-`39` | Application diagnostics | Six Channel Diagnostics reports, addressed as `30 + channel`; `36`-`39` unassigned. |
| `40`-`255` | Unassigned expansion | None. |

Ranges group reports by purpose; they do not enforce access. ID `0` is unused. Sharing the ranges does not mean sharing
payload layouts: a host must identify the product before decoding product-specific fields.

| Report ID | Name | Type | Direction | Payload | Purpose |
| --------- | ---- | ---- | --------- | ------- | ------- |
| `1` | Device Identity and Capabilities | Feature | Host reads | 27 bytes | Identity, capabilities, total/reserved RAM. |
| `2` | Board Configuration | Feature | Host reads | 36 bytes | Stored profile, pins, reset cause, buffer capacities. |
| `3` | Board Telemetry | Feature | Host reads | 20 bytes | Temperature, LED, uptime, service load, running clock. |
| `10`-`15` | Channel Snapshots (CDC0–CDC5) | Input / Feature | Device publishes / host reads | 30 bytes | State, measurement validity, applied settings, traffic. |
| `20` | Board Action and Result | Feature | Host reads/writes | 4 bytes | LED/reboot/save actions and results. |
| `30`-`35` | Channel Diagnostics (CDC0–CDC5) | Feature | Host reads | 24 bytes | RX fault history, last configuration error, buffer peaks. |

All sixteen report IDs are always present. Unavailable features are signalled by capability flags, not by missing
reports. Writes to read-only reports are rejected without side effects.

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

### Identity and Capabilities

**Discovery.** Every product using this convention shares a three-byte prefix: the layout version at offset 0 and the
little-endian product ID at offsets 1-2. Read the descriptor-declared length, check the prefix, choose the matching
product contract, and only then validate the full report. Never decode a known product's unsupported version with an
older layout. Show an unknown product as `Unknown product (0xNNNN)`, and enable no product-specific commands for it.

| Product ID | Meaning |
| ---------: | ------- |
| `0x0000` | Reserved / unspecified. |
| `0x0001` | PicoUart, across supported board/MCU variants. |
| `0x0002`-`0xfffe` | Assign as other project products are introduced. |
| `0xffff` | Reserved. |

**UID.** Display it as **Serial / UID**: uppercase hex, two characters per byte, matching the 16-character USB serial.
On RP2040 it is the QSPI flash UID; on RP2350 it is the SDK platform identity.

**Board model.** This is the configured build target; the firmware does not detect hardware. Keep unknown MCU and board
numbers as they are rather than inventing names.

| Board model | Build target | MCU | Support status |
| ----------: | ------------ | --- | -------------- |
| `0` | Unknown / unspecified | Reported separately | No specific board identity claimed. |
| `1` | Pico | RP2040 | Existing firmware target. |
| `2` | Pico W | RP2040 | Identifier reserved; target support not implemented or qualified. |
| `3` | Pico 2 | RP2350 | Existing firmware target. |
| `4` | Pico 2 W | RP2350 | Identifier reserved; target support not implemented or qualified. |

**Firmware version.** A `v1.2.3` tag reports `1.2.3`. The USB `bcdDevice` field carries only major.minor. Use the
version for diagnostics only: choose decoding by product and layout, and choose operations by capability flags.

**Channel mask.** Report 2 owns the channel count (six for PicoUart). Reject a hardware mask with bits set above that
count. PicoUart reports `0x03`, so CDC0 and CDC1 use hardware UARTs and the rest use PIO.

| Capability bit | Meaning |
| -------------: | ------- |
| 0 | Board identification LED available. |
| 1 | Confirmed standalone reboot enabled by `PICO_UART_ALLOW_HID_RESET`. |
| 2 | Boot-configuration save-and-reboot available. |
| 3-7 | Reserved, zero. |

Clock-profile mask bits 0-4 match the profile IDs in report 2. Bit 0 is always set, and bits 5-7 are zero. A set bit
means the firmware accepts the request. It says nothing about stability.

### Firmware RAM Reservation

Show `ram_reserved_bytes` / `ram_total_bytes` as **Firmware RAM reserved / total**. This is build-time accounting: the
linked SRAM through `__end__`, the scratch sections, and both stack reservations. It is not free heap or runtime
memory pressure. Reject a zero total or a reservation larger than the total.

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

### Channel Hardware Map

Each entry is a GPIO number (not a header pin), and `255` means unassigned. Entries are in channel order, from the
firmware's board configuration. An assigned RTS/CTS pin does not mean flow control is running.

DTR is a CDC session signal and is not part of this map. If report 2 cannot be read, treat the pins and channel count
as unknown. Never substitute GPIO 0.

### Shared Buffer Capacities

All four capacities are nonzero and the same for every channel. They give the storage behind the matching peaks in
report 30-35, not total USB in-flight storage. Show peak percentages only when report 2 is valid; otherwise show raw
bytes.

### Reset Cause

| Value | Meaning |
| ----: | ------- |
| `0` | Unknown or not reliably distinguishable. |
| `1` | Power-on, when reliably identified by the platform. |
| `2` | Watchdog reset, without a reliably established software-requested cause. |
| `3` | Software-requested reset, when reliably established by firmware/platform evidence. |
| `4` | Watchdog recovery after firmware recorded a HardFault in watchdog scratch storage. |
| `5` | Power-on or brownout; RP2040 provides a combined hardware indication. |
| `6` | Brownout, independently identified by the platform. |
| `7` | External RUN-pin reset. |
| `8` | Debugger reset, identified by platform reset evidence. |
| `9` | User-requested standalone board reboot, identified by a firmware marker. |
| `10` | Configuration-save reboot after successful flash verification, identified by a firmware marker. |
| `11`-`255` | Unassigned; hosts preserve the numeric value and display it as unknown. |

The firmware reports a cause only when it has evidence; otherwise it reports Unknown, which is not a fault by itself.
RP2040 cannot tell power-on from brownout, so it reports `5`. Reboot and save markers live in watchdog scratch
registers and are consumed at boot. They identify the firmware path, not which host asked for the reboot. A recorded
HardFault takes priority over those markers.

### Boot Configuration

| Profile ID | Requested clock / meaning | Targets |
| ------: | ----- | ------- |
| `0` | SDK default; no clock reconfiguration | Both |
| `1` | 150 MHz | Both |
| `2` | 200 MHz | Both |
| `3` | 250 MHz | Both |
| `4` | 280 MHz | Both |
| `5`-`254` | Unassigned; invalid in the currently specified layout | None |
| `255` | Unavailable sentinel for `stored_profile` readback only; never a save request | None |

`stored_profile` is only a request; report 3 has the running clock. Show the two separately, and
never infer the profile from the frequency.

At boot the firmware picks the highest supported frequency at or below the request. The current build caps RP2040 at
250 MHz and RP2350 at 280 MHz. If no frequency is eligible, or the clock change fails, it falls back to SDK defaults.

Invalid or erased storage reads as `255` and boots with SDK defaults; flash is not rewritten. Watchdog recovery can also
fall back to defaults while keeping the stored request. None of this qualifies an overclock as stable on a given board.

### Save and Reboot Safety

Saving a profile writes flash and reboots the board, so it is guarded:

- The profile must be advertised, the request must carry the confirm flag, and a non-default profile also needs the
  overclock acknowledgement. Anything else is rejected without touching flash.
- Every CDC port must be closed (DTR low) when the request is accepted, and again when it runs. Also stop external UART
  traffic. Flash erase stalls both cores, and the reboot drops any queued bytes.
- The write happens in the USB poll loop, outside the USB callback. Core 1 is locked out using the SDK flash-safe
  API. Only the final flash sector is erased. The record is CRC-protected and checked in full before the result reports
  Completed. Saving the profile that is already stored skips the write.
- Completed means the profile was verified in flash; it does not promise the next boot succeeded. Reconnect and read
  report 2 to confirm. An interrupted write boots SDK defaults, because storage is one sector, not a journal.

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

| Display | Interpretation |
| ------- | -------------- |
| Running clock | SDK-configured frequency; the requested profile is separate. |
| Estimated die temperature | Internal sensor estimate in Celsius, normally shown to one decimal place. |
| USB service load (Core 0) / UART service load (Core 1) | Work completed by each core during the sampling window; `65535` means warming up/unavailable. |
| LED | Off/On when supported; otherwise Unavailable. |
| Uptime | Time since boot; compare readings only for the same identified device. |

If any mandatory measurement fails, the whole GET fails. The firmware never returns a partial or zero-filled report.
The load sentinel `65535` and an absent LED are valid values and not failures. When a read fails, the host marks live
values stale, disables LED controls, and keeps static data from reports 1 and 2.

**Service load** is the wall time spent in service passes that did real work, divided by the measurement window.
Each core publishes about once a second and reports `65535` before its first window. Core 0 counts USB, CDC, control,
and HID work; core 1 counts UART data, control, and error recovery. Idle polling and waiting on DMA are excluded. It
measures service workload, not CPU utilization or spare throughput. Reject values above `10000` other than the sentinel.

## Report ID 20: Board Action and Result

A SET requests one action, and a GET returns the latest board-wide result. Both use four bytes.

| Offset | Size | SET_REPORT field | GET_REPORT field |
| -----: | ---: | ---------------- | ---------------- |
| 0 | 1 | `version = 1` | `version = 1` |
| 1 | 1 | `action`: action from the table below. | Latest recognizable `action`; `0` when idle or unrecognized. |
| 2 | 1 | `flags`: bit 0 confirms reboot/save; bit 1 acknowledges unsafe overclocking. Bits 2-7 zero. | `status`, defined below. |
| 3 | 1 | `profile`: profile to save for action 4; zero for other actions. | `error_reason`: action rejection or execution failure. |

Always build a fresh SET. Bytes 2-3 mean different things in each direction, so a GET response is not a writable
image. A GET never runs or repeats an action.

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
| `1` | Pending | Accepted, not yet complete. |
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

Rejected requests have no side effects. An accepted SET publishes its result, either Pending or an immediate Completed,
before the USB transfer finishes.

### Action Timing and Host Rules

- A completed SET transfer does not prove the action ran. Read report 20 right away and then every 50 ms while it is
  Pending, for up to 3000 ms. Give each SET and GET its own 3000 ms deadline. WebHID cannot cancel a stuck call, so
  ignore late completions and never start an overlapping write.
- Reboot and save reset the board 1000 ms after publishing Completed. Later reads and writes neither delay nor cancel
  this.
- **A timeout or disconnect means the outcome is unknown, not failed.** Never retry a reboot or save automatically.
  Reconnect, read report 2, and let the user decide. The Direct Dashboard remembers an unconfirmed save by board UID.
  It requires a fresh report 2 read and explicit acknowledgement before it allows another save.
- WebHID open and close timeouts do not cancel the browser operation. Keep reconnect disabled until the operation
  settles. If an open completes late, close it; if that fails, unplug the board.

## Reports 10-15: Channel Snapshots

Report `10 + channel` covers CDC0–CDC5. The firmware publishes an Input report for each channel about every **500 ms**.
A Feature GET returns the same layout on demand.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version `1`. |
| 1 | 1 | `channel_state` | Lifecycle summary defined below; not a history flag. |
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
missing or stale report is a host-side Unknown, not a wire value. Show Error as "attention" and read the diagnostics
report for the reason. The request lifecycle behind Configuring and Error is described in the
[control plane design](../control-plane-design.md).

### UART Measurement State

| Value | State | Meaning |
| ----: | ----- | ------- |
| `0` | Unavailable | No coherent UART statistics sample exists; UART counters are zero placeholders. |
| `1` | Current | Coherent UART statistics acquired for this capture. |
| `2` | Cached | Last accepted UART statistics reused; UART rates unavailable. |
| `3`-`255` | Unassigned | Invalid in this layout. |

UART statistics come from the core-1 backend through a lock-free snapshot that can fail. When it fails, the firmware
reuses the last good sample (Cached) and never substitutes zeros. Current describes the sample, not the backend: a
stopped port can still have a Current final sample. The CDC counters are owned by core 0 and are always fresh.

### Traffic Counters

| Direction | Counter | Counts |
| --------- | ------- | ------ |
| Host → UART | `cdc_rx_bytes` | Bytes consumed from TinyUSB into UART TX ring. |
| Host → UART | `uart_tx_bytes` | Peripheral/FIFO handoff: HW/PIO DMA completion or PIO polling FIFO writes, not final stop-bit transmission. |
| UART → Host | `uart_rx_bytes` | DMA bytes published into RX ring, including bytes subsequently overwritten. |
| UART → Host | `cdc_tx_bytes` | TinyUSB acceptance, not confirmed host receipt. |

These count handoffs rather than delivered bytes. Closing a port or resetting USB can discard bytes already counted, so
a difference between counters is not a loss measurement. Use the diagnostics counters and payload checks for that. The
counters run from boot and wrap at 2^32, and USB reconnects do not reset them. The applied baud rate is a setting, not a
measured wire rate; show it as **Last applied** while the port is stopped.

### Snapshot Consistency

Each report is captured coherently. The baud rate, framing, and state come from one applied configuration, and the
four counters are read together at the interval boundary. Reports for different channels are separate captures, as are
snapshots and diagnostics; never assume two reports are atomic with each other.

### Sequence, Interval, and Rates

- Each channel has its own sequence and interval. A sequence number is used up only when USB accepts the Input report,
  so the first accepted report is `1`. A Feature read returns the last committed sequence, or `0` before the first.
  USB reset restarts the sequence but not the counters.
- `interval_ms` is the time since this channel's previous accepted Input, capped at `65535`. Values `0` and `65535`
  cannot produce a rate.
- When the endpoint is busy, publication is delayed; old snapshots are never queued or sent in a burst. Due channels
  are served round-robin.
- **Host rate math:** take the counter delta modulo 2^32 and divide by the device's `interval_ms`. The first sample
  after a (re)connect only sets the baseline. A sequence gap, a stale stream, or a non-Current UART sample discards that
  channel's rate window while keeping its totals; other channels are unaffected. Combine windows by summing bytes and
  intervals rather than averaging rates. Feature reads never set a rate baseline and are not summed into totals.
- Wrap-safe deltas need fewer than 2^32 bytes between samples, which is hours at UART speeds. A 16-bit sequence cannot
  detect a loss of exactly 65536 reports, so rebaseline after any long pause.

## Reports 30-35: Channel Diagnostics

Report `30 + channel` is a read-only Feature with no Input publication. Read it when opening a channel's details or
investigating an Error. For unattended alerts, a host may poll it slowly, but only when the user enables that. Reading
it never clears history or affects snapshot sequences and baselines.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version `1`. |
| 1 | 1 | `uart_measurement_state` | Same Unavailable/Current/Cached enum as routine snapshots, for this capture. |
| 2 | 1 | `uart_sample_age_100ms` | UART acquisition age in 100 ms units, rounded down; 255 means at least 25.5 seconds or no sample. |
| 3 | 1 | `last_control_error_reason` | Historical last configuration error, not an active-failure flag. |
| 4 | 4 | `rx_overrun_count` | Saturating known dropped UART RX bytes. |
| 8 | 4 | `rx_error_count` | Saturating controller error observations, not bad bytes/frames. |
| 12 | 4 | `last_fault_uptime_s` | Latest observed RX fault or recorded configuration error, in saturating uptime seconds. |
| 16 | 2 | `uart_tx_ring_peak_bytes` | Boot-lifetime observed UART TX-ring peak, in bytes. |
| 18 | 2 | `uart_rx_ring_peak_bytes` | Boot-lifetime observed UART RX-ring peak, in bytes. |
| 20 | 2 | `cdc_tx_fifo_peak_bytes` | Boot-lifetime observed device-to-host FIFO peak. |
| 22 | 2 | `cdc_rx_fifo_peak_bytes` | Boot-lifetime observed host-to-device FIFO peak. |
| **Total** | **24** | | **25 bytes including the report ID.** |

| Counter | User-facing label |
| ------- | ----------------- |
| `rx_overrun_count` | Known RX bytes dropped |
| `rx_error_count` | RX controller error observations |

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

This field records history and does not mean the port is failing now. It keeps its value through later successes, port
close and reopen, and USB reconnects, and clears only at boot. Label it **Last configuration error**.

### Fault History

- The two RX counters saturate rather than wrap, and nothing on the device clears them. `rx_overrun_count` counts known
  dropped bytes. `rx_error_count` counts sticky error flags seen when sampled, so several bad frames can count as one;
  it is not a per-byte or per-frame total.
- `last_fault_uptime_s` is the uptime of the latest RX fault or configuration error. It never decreases within a boot.
  A startup failure on its own does not advance it.
- "No history" means both counters, the reason, and the timestamp are zero. A reason with zero RX counts is valid.
- The USB core samples faults about every 100 ms, whether or not anyone is reading.
- Nonzero counts are history and say nothing about the present. Possible causes include a floating RX line, a break, a
  wrong baud rate, or wiring faults; the counters cannot tell which.
- There is no boot ID. After reconnecting, start new comparison baselines rather than claiming exact fault counts across
  the gap.

### Measurement Age and Peaks

| State | Diagnostic age and measurements |
| ----- | ------------------------------- |
| Unavailable (`0`) | Age 255; UART-derived RX counts and ring peaks are zero placeholders, not evidence of fault-free operation. |
| Current (`1`) | Age zero; coherent UART statistics acquired for this capture, including a stopped backend when safely captured. |
| Cached (`2`) | Reuse last accepted UART statistics; age may be zero due to rounding or saturate at 255. |

Peaks are the largest occupancy seen at the firmware's sampling points, so a brief maximum can be missed. Label them
**Observed peak since boot** and compare them with the capacities in report 2. A full peak does not by itself mean
current congestion or data loss. Peaks survive port close and reopen.

## Host Polling and Validation

1. Read report 1, check the [discovery prefix](#identity-and-capabilities), and validate the full report before sending
   any command. PicoUart requires version 1 and exactly 27 bytes.
2. Read report 2 once per connection. Check that the channel count is 6 and use it to check report 1's channel mask.
3. Poll report 3 every 500-1000 ms.
4. Consume Input reports 10-15 for snapshots and traffic deltas. Feature reads give the current state on demand.
5. Read reports 30-35 on demand, or slowly if the user enables alerts.
6. Send actions only through report 20, then read report 20 until the result is final.

At steady state that is one or two telemetry reads and about twelve channel Inputs per second. Mark a channel stale
after three seconds with no Input. A read error invalidates only the data it would have refreshed. After a disconnect
or reboot, drop every cache and pending action and start again at step 1.

For every report, validate its type, ID, exact length, version, reserved bits, enum values, and field consistency.
Reject unknown layouts outright; there is no negotiation.

### Conformance Examples

Report-20 payloads, without the report-ID byte. Assume the LED and save capabilities are present, CDC ports are
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
| Another write while Pending or Completed awaiting reboot | Reject the transfer; preserve the existing operation, result, and reset deadline. |
| Reboot/save SET times out or disconnects | Treat outcome as unconfirmed; do not automatically retry. |
| First channel publication fails, a Feature read follows, then publication succeeds | Failed candidate uses sequence 1 without committing; Feature returns 0; first accepted Input returns 1. |
| Channel sequence `65535` followed by `0` | Normal modulo-65536 continuity, not a gap. |
| Input at time T; Feature read at T+250 ms; next Input at T+500 ms | Feature read does not commit a baseline. Next Input still covers 500 ms from T and includes all interval bytes once. |
| Channel 0 sequence jumps from 10 to 12 while channel 1 advances normally | Recover channel 0's cumulative totals, discard its rate window, and leave channel 1 unaffected. |
| Line coding changes during snapshot capture | Report either coherent old or coherent new applied configuration, never a mixture. |

## Versioning

Version `1` is used by published firmware; earlier development layouts are not compatibility targets. An incompatible
change to a report's length, units, meaning, or validation requires a version bump and a matching update to this
document. The descriptor, firmware, and the Python, .NET, and WebHID clients change together. A host must explicitly
support a layout before decoding it.

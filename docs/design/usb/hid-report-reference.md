# HID Report Reference

This document is the HID protocol reference: static device information, board controls, live telemetry,
per-channel monitoring snapshots, and on-demand channel diagnostics. It evolves with the protocol; version numbers
identify individual report layouts, not the scope or lifetime of this document.

## Report ID Allocation

The ranges below are a reusable convention for Pico projects; the assignments shown are specific to PicoUart.
Other projects may assign application reports for sensors, motors, GPIO, or other functions.

| IDs | Generic role | PicoUart assignments |
| --- | ---- | ----------- |
| `1`-`9` | Device information and monitoring | `1` Identity, `2` Board Configuration, `3` Telemetry; `4`-`9` unassigned. |
| `10`-`19` | Application monitoring | Six Channel Snapshots, addressed as `10 + channel`; `16`-`19` unassigned. |
| `20`-`29` | Device and application control | `20` Board Action & Result; `21`-`29` unassigned. |
| `30`-`39` | Application diagnostics | Six Channel Diagnostics reports, addressed as `30 + channel`; `36`-`39` unassigned. |
| `40`-`255` | Unassigned expansion | None. |

Ranges group monitoring and control; they do not enforce access. Report 20 uses SET for actions and GET for results.
The descriptor and firmware enforce types/operations. ID `0` is unused; unassigned IDs require no padding.

Reusing these ranges does not imply shared payload layouts. Hosts must identify the product and select its report
contract before decoding project-specific fields; matching report IDs and layout versions alone is insufficient
across products. Common layouts may be shared explicitly where their meanings match.

PicoUart exposes one vendor-defined USB HID interface named `Status Monitor`. HID provides device information,
board controls, and diagnostics for the six UART bridges. UART data and line configuration remain on the matching
USB CDC interface. DTR opens start the matching backend; bridging waits for readiness and initial configuration.
DTR closes stop bridging and request worker-owned backend shutdown. Closed channels are normally stopped, not
initializing. DTR state is also checked for configuration-save safety.

The interface uses vendor usage page `0xFF00`, vendor usage `0x01`, and no boot protocol. The device is identified
as USB `cafe:4010` (the project's unallocated lab identity, permitted for published project artifacts).
One HID interface follows the twelve CDC control/data interfaces. The USB product name is `PicoUart`.
CDC interface strings advertise backend limits: `CDC0 HW` / `CDC1 HW` and `CDC2`-`CDC5 PIO 8N1`.

## Ownership

| Operation | USB interface | Contract |
| --------- | ------------- | -------- |
| UART data | CDC0 through CDC5 | Transfers bytes to and from the matching UART. |
| Baud, data bits, parity, stop bits | CDC line-coding request | Queued to the matching backend. USB completion precedes firmware validation; rejected requests set `control_error`. PIO accepts 8N1 only. |
| Identity, capabilities, telemetry, and diagnostics | HID | Read-only monitoring data. |
| Board identification LED | HID report 20 | Explicit On/Off actions when an LED is available; result read from the same report. |
| Standalone board reboot | HID report 20 | Capability-gated action with explicit confirmation; no separate arm transaction. |
| Save boot configuration and reboot | HID report 20 | Validated clock profile, explicit confirmation, overclock acknowledgement, closed CDC sessions, verified flash write, then automatic reboot. |

HID does not select or reconfigure a UART, change GPIO mapping, or alter ring-buffer behavior.
Reading a channel's diagnostic report does not change transport ownership.

## Report IDs

These are logical groups, not separate HID usage pages or mandatory dashboard tabs.

| Report ID | Name | Type | Direction | Payload | Purpose |
| --------- | ---- | ---- | --------- | ------- | ------- |
| `1` | Device Identity & Capabilities | Feature | Host reads | 27 bytes | Identity, capabilities, total/reserved RAM. |
| `2` | Board Configuration | Feature | Host reads | 36 bytes | Stored profile, pins, reset cause, buffer capacities. |
| `3` | Board Telemetry | Feature | Host reads | 20 bytes | Temperature, LED, uptime, service load, running clock. |
| `10`-`15` | Channel Snapshot — CDC0–CDC5 | Input / Feature | Device publishes / host reads | 30 bytes | State, measurement validity, applied settings, traffic. |
| `20` | Board Action & Result | Feature | Host reads/writes | 4 bytes | LED/reboot/save actions and results. |
| `30`-`35` | Channel Diagnostics — CDC0–CDC5 | Feature | Host reads | 24 bytes | RX fault history, last configuration error, buffer peaks. |

Names are documentation and host-display labels, not additional wire fields.

There are sixteen unique IDs. `10 + channel` addresses matching Input/read-only Feature snapshots;
`30 + channel` addresses read-only Feature diagnostics, with no Input publication. No mutable selector is used.

**Wire rules:** Multi-byte integers are little-endian; the UID is an ordered byte sequence. Offsets/sizes exclude
the transport report-ID byte. Every report fits within 64 bytes including ID. The layouts currently specified here
use version `1`, independent of firmware semantic version. Within the selected product contract, report type, ID,
length, and version identify the layout; no signature or channel-number field is needed.

## Report ID 1: Device Identity & Capabilities

Read once per connection; static until reboot/re-enumeration. Live measurements belong to report 3 and action
results to report 20.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 2 | `product_id` | Project-owned product-family identifier; independent of USB VID/PID. |
| 3 | 8 | `device_uid` | Opaque SDK board identity bytes, in USB serial-string order. |
| 11 | 1 | `firmware_major` | Firmware semantic version major component. |
| 12 | 1 | `firmware_minor` | Firmware semantic version minor component. |
| 13 | 1 | `firmware_patch` | Firmware semantic version patch component. |
| 14 | 1 | `mcu` | Silicon family: `1` RP2040; `2` RP2350. |
| 15 | 1 | `board_model` | Configured firmware build target, defined below; not automatic board detection. |
| 16 | 1 | `hardware_channel_mask` | Within report 2's channel count, `1` means hardware UART and `0` means PIO UART. |
| 17 | 1 | `capabilities` | Static board-control capability flags. |
| 18 | 1 | `clock_profile_mask` | Firmware-accepted boot profile request bits; see report 2. |
| 19 | 4 | `ram_total_bytes` | Total SRAM for the MCU/linker layout; nonzero, not free RAM. |
| 23 | 4 | `ram_reserved_bytes` | Static and reserved SRAM for this firmware build, not live heap/stack use. |
| **Total** | **27** | | **28 bytes including the report ID.** |

### Firmware RAM Reservation

Display `ram_reserved_bytes` against `ram_total_bytes` from this same report as **Firmware RAM reserved / total**,
in KiB and optionally as a percentage. Reject reservations exceeding total RAM or a zero total.
This is build accounting, not runtime memory pressure, free heap, or stack high-water use.
Reservation includes linked SRAM from `SRAM_BASE` through `__end__`, scratch X/Y linked sections, and both SDK stack
reservations. It excludes remaining heap capacity and does not measure live allocations or stack use.
Total SRAM is `SRAM_END - SRAM_BASE`, including scratch banks. Use the existing authoritative firmware accounting;
do not introduce a second calculation for this report.

### Identity and Capabilities

Display `device_uid` as **Serial / UID**: two uppercase hex characters per byte in order, matching the 16-character
USB serial. It is the SDK board ID, not an integer or host/browser-generated ID. RP2040 uses the external QSPI flash
UID, not silicon serial; RP2350 uses SDK platform identity. Product names come from this registry; retain a readable
USB product descriptor for OS device lists.

| Product ID | Meaning |
| ---------: | ------- |
| `0x0000` | Reserved / unspecified. |
| `0x0001` | PicoUart, across supported board/MCU variants. |
| `0x0002`-`0xfffe` | Assign as other project products are introduced. |
| `0xffff` | Reserved. |

Product IDs identify families, not releases, MCUs, or compatibility. Products using this convention share report 1's
three-byte discovery prefix: layout version at offset 0 and little-endian product ID at offsets 1-2.
Read the descriptor-declared Feature length and require at least that prefix before inspecting it; do not assume
PicoUart's 27-byte payload during discovery. The prefix alone does not validate the complete identity.
Select the supported product/layout contract, then validate its exact length and all remaining fields before
using capabilities or issuing commands. A known product with an unsupported version is an explicit unsupported
layout, not permission to decode a known prefix's remaining fields using an older layout.
Preserve these prefix offsets across products and future identity versions using this convention.
Display unknown products as `Unknown product (0xNNNN)` without enabling product-specific commands.
Retain unknown MCU/board numbers without inventing names or discarding valid identity.

| Board model | Build target | MCU | Support status |
| ----------: | ------------ | --- | -------------- |
| `0` | Unknown / unspecified | Reported separately | No specific board identity claimed. |
| `1` | Pico | RP2040 | Existing firmware target. |
| `2` | Pico W | RP2040 | Identifier reserved; target support not implemented or qualified. |
| `3` | Pico 2 | RP2350 | Existing firmware target. |
| `4` | Pico 2 W | RP2350 | Identifier reserved; target support not implemented or qualified. |

Board model is the configured build target, not hardware detection. A W model does not advertise implemented
Wi-Fi/Bluetooth operations; capabilities describe actual functionality.

Tag `v1.2.3` builds advertise firmware `1.2.3`. USB descriptor `bcdDevice` still carries only major.minor as BCD.
Report 2 is the single source of `channel_count`. The hardware mask has no bits set above that count; derive the
PIO mask within that width instead of sending a redundant second mask. Current PicoUart hardware mask is `0x03`.
Hosts need a valid report 2 before interpreting the backend mask; identity and board capabilities remain usable
if that read fails.

| Capability bit | Meaning |
| -------------: | ------- |
| 0 | Board identification LED available. |
| 1 | Confirmed standalone reboot enabled by `PICO_UART_ALLOW_HID_RESET`. |
| 2 | Boot-configuration save-and-reboot available. |
| 3-7 | Reserved, zero. |

Clock-profile mask bits 0-4 describe profile requests accepted by this firmware; bits 5-7 are reserved and zero.
Bit 0 must be set. Acceptance does not guarantee the requested frequency or safe overclock operation on every board.
Keep firmware version for diagnostics; select decoding by supported product/layout and operations by capabilities,
not firmware-version guesses. No extra global protocol version or alignment padding is needed.

## Report ID 2: Board Configuration

Read-only; reject SET. Cache once per connection and reread after disconnect/reboot. Fields describe boot-time
configuration, immutable hardware, and the reset that began this boot. Saving does not update them before reboot.

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

### Shared Buffer Capacities

Capacities must be nonzero and equal across channels; unequal capacities require a revised layout.
Encode from authoritative firmware/TinyUSB configuration, without duplicated constants. Each capacity describes its
matching peak's storage, not DMA staging or total USB in-flight storage. Validate peaks against advertised capacities;
show percentages only with valid report-2 metadata, otherwise bytes without guessed capacities. No buffers are resized.

### Reset Cause

Capture at startup: reset cause is not clock-selection reason. These protocol values do not imply that every MCU
can distinguish all causes; verify mappings separately before implementation.

| Value | Meaning |
| ----: | ------- |
| `0` | Unknown or not reliably distinguishable. |
| `1` | Power-on, when reliably identified by the platform. |
| `2` | Watchdog reset, without a reliably established software-requested cause. |
| `3` | Software-requested reset, when reliably established by firmware/platform evidence. |
| `4`-`255` | Unassigned. |

Do not infer power-on from absence of a watchdog indication. Intentional reboot may use the watchdog: prefer an
evidenced software-requested cause, otherwise known watchdog or Unknown. Software-requested does not prove which
host action caused reset.
Current firmware reports evidenced Watchdog or Unknown; it does not infer power-on or software-requested reset
from missing SDK watchdog markers.

### Channel Hardware Map

Each signal uses one byte: a configured GPIO number or `255` for unassigned. These are GPIO identifiers, not physical
header-pin numbers. Entries are always in logical channel order, not backend order. Use the immutable firmware
board configuration as the source; do not duplicate a separate pin table in the encoder or infer assignments from
the board model. Unassigned or unsupported RTS/CTS signals are `255`, not invented pins.

RTS and CTS are the UART flow-control signals; DTR is host-side CDC session control, not another GPIO in this map.
A pin assignment does not establish that its peripheral is running or that flow control is active.
HID cannot write or select pins; this report does not add flow-control support or a new configuration command.
A failed read surfaces an explicit error and leaves boot configuration, channel count, and assignments unavailable;
never substitute GPIO zero or invented profiles. Future products may define product-specific report-2 layouts.

### Boot Configuration

`stored_profile` is a request, not the running clock. Invalid/erased storage reports `255` and selects SDK defaults
without rewriting flash. Frequency reduction/recovery preserves a valid stored request and is not itself a save
failure. Storage-validation and clock-selection reasons remain internal; `reset_cause` is independent.

Use report 3's `system_clock_hz` for running frequency and report 20 for save results. Do not substitute the request
when telemetry is unavailable or infer an active profile from frequency: different profiles can yield the same clock.
Profile IDs are stable one-byte identifiers, not array positions. Their meaning is shared by RP2040/RP2350;
"Both" does not promise the requested operating point is qualified on every board.

For a non-default request, firmware selects the highest eligible frequency no greater than the requested frequency,
using a deterministic board/build policy and supported clock configurations. It must not blindly attempt a requested
overclock or probe stability by trial during boot. If no eligible frequency exists or clock application fails, use
SDK defaults. Watchdog recovery may also select SDK defaults under the firmware's recovery policy.
Profile 0 directly selects SDK defaults. SDK-default fallback is an explicit exception to the requested upper bound;
it is not guaranteed to be lower than every request.
The current build policy permits profile selection through 250 MHz on RP2040 and 280 MHz on RP2350; a stored
280 MHz request on RP2040 is retained while selecting 250 MHz. This policy is not a hardware stability qualification.

Display requested and running clocks separately. Save completion proves persistence, not achieved frequency or
stability; non-default operating points require exact-board hardware qualification.

| Profile ID | Requested clock / meaning | Targets |
| ------: | ----- | ------- |
| `0` | SDK default; no clock reconfiguration | Both |
| `1` | 150 MHz | Both |
| `2` | 200 MHz | Both |
| `3` | 250 MHz | Both |
| `4` | 280 MHz | Both |
| `5`-`254` | Unassigned; invalid in the currently specified layout | None |
| `255` | Unavailable sentinel for `stored_profile` readback only; never a save request | None |

Firmware settings remain a private cache accessed through `boot_config_get`, `boot_config_set`, and `boot_config_save`.
The same profile IDs are used by settings, storage, and HID. Firmware clock selection and validation stay within that module;
hosts must not reject a defined, advertised profile solely because of MCU family.

### Save and Reboot Safety

Accept only an advertised profile with explicit confirmation and the required overclock acknowledgement.
Missing confirmation/acknowledgement, unknown or unadvertised profiles, invalid versions, lengths, reserved fields,
or flags must not write flash.
Every CDC DTR session must be closed both at acceptance and execution.
Stop external UART traffic too: DTR closure requests asynchronous backend shutdown rather than acknowledging physical
peer quiescence. Flash erase pauses CPU servicing and a successful save reboots the device; neither operation guarantees
delivery of queued or externally transmitted bytes.

Queue the operation for the USB poll loop, outside the TinyUSB control callback.
The SDK flash-safe execution API disables interrupts and locks out core 1, registered as a lockout victim during
UART worker initialization. Erase only the reserved final sector; its first page contains the CRC-protected record
and the remainder stays erased. Full-sector validation and page comparison must succeed before reporting Completed.
Saving an already valid identical profile does not erase/program again.

The stored-profile snapshot remains unchanged until reboot.
Completed for a save means the submitted profile passed flash verification, not that it has booted successfully.
Reconnect and read report 2 to verify the resulting boot configuration.
Interrupted writes fall back to SDK defaults, not a previous record: storage is a single sector, not a redundant journal.
Non-default profiles remain experimental and require exact-board HIL; save success does not establish stability.

## Report ID 3: Board Telemetry

Live board measurements only; static memory information belongs in report 1, configuration in report 2,
and action results in report 20.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `led_flags` | Bit 0: identification output On. Other bits zero. |
| 2 | 2 | `temperature_centidegrees_celsius` | Signed internal RP2 sensor estimate, in hundredths of a degree Celsius. |
| 4 | 8 | `uptime_ms` | SDK monotonic milliseconds since boot. |
| 12 | 2 | `core0_service_load_basis_points` | `10000` = 100%; `65535` unknown. |
| 14 | 2 | `core1_service_load_basis_points` | Same scale and sentinel. |
| 16 | 4 | `system_clock_hz` | Current SDK-reported `clock_get_hz(clk_sys)`, nonzero. |
| **Total** | **20** | | **21 bytes including the report ID.** |

| Display | Interpretation |
| ------- | -------------- |
| Running clock | SDK-configured frequency, not independently calibrated oscillator measurement; separate from requested profile. |
| Estimated die temperature | Normally one decimal place; encoding precision is not accuracy, ambient temperature, or proof of thermal safety. |
| USB service load — Core 0 / UART service load — Core 1 | Workload proxies, not CPU utilization or remaining capacity; `65535` means warming up/unavailable, not zero. |
| LED | Off/On only if capability exists; otherwise Unavailable even though flags are zero. |
| Uptime | Device uptime, not session duration; a decrease suggests reset only for the same identified device. |

Mandatory telemetry fields must be acquired successfully for a GET to succeed. If temperature acquisition or another
mandatory measurement fails, fail the entire Feature GET; never return a successful partial report, zero substitute,
or retained failed measurement as fresh. The defined service-load unknown sentinel and absent-LED representation
remain valid responses, not whole-report failures. Service load follows its published measurement windows below.

Timestamp successful reads and expose age. On failure, clear/mark this report's live values unavailable or stale and disable LED
controls until recovery; preserve independently cached static data. No wire timestamp/sequence is needed for GET
polling. Successful USB reads do not prove UART-worker progress. Overflow counts remain in channel diagnostics.

### Runtime Interpretation

Service load is elapsed wall time of service passes that handle work divided by actual measurement-window duration,
in basis points (0.01%). Each core publishes after at least one second; before its first window it reports `65535`.
Empty polling passes are excluded. Core 0 counts USB events, CDC transfers, control progress, and HID publication;
core 1 counts UART data progress, control handling, and error recovery. Waiting for DMA alone does not count as work.

The complete marked pass is charged once, including polling overhead and interrupts during it.
This is a workload proxy, not total CPU utilization, instruction profiling, line occupancy, or calibrated remaining
throughput. Both cores still busy-poll. Monitoring and noisy RX/error handling can create nonzero load without
application traffic. Replace the misleading current host name `cpu_usage_basis_points` with service-load naming
consistently in JSON, models, and labels. Reject loads above `10000` except the unknown sentinel.

## Report ID 20: Board Action & Result

SET requests one action; GET returns the latest device-global result. Both directions use four payload bytes.

| Offset | Size | SET_REPORT field | GET_REPORT field |
| -----: | ---: | ---------------- | ---------------- |
| 0 | 1 | `version = 1` | `version = 1` |
| 1 | 1 | `action`: action from the table below. | Latest recognizable `action`; `0` when idle or unrecognized. |
| 2 | 1 | `flags`: bit 0 confirms reboot/save; bit 1 acknowledges unsafe overclocking. Bits 2-7 zero. | `status`, defined below. |
| 3 | 1 | `profile`: profile to save for action 4; zero for other actions. | `error_reason`: action rejection or execution failure. |

Construct a fresh SET request explicitly. Never modify and write back a GET response: offsets 2-3 have different
meanings, and a readback is not a writable configuration image. GET does not execute or repeat an action.

| Action | Name | Required flags | Behavior |
| -----: | ---- | -------------- | -------- |
| `1` | LED Off | `0` | Set identification LED Off. |
| `2` | LED On | `0` | Set identification LED On. |
| `3` | Reboot | `0x01` | Schedule a watchdog reboot; no separate arm transaction. |
| `4` | Save profile and reboot | `0x01` for profile 0; `0x03` for non-default profiles | Validate and save the profile, then automatically reboot. |

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

Hosts validate status/error-reason consistency. Idle requires action zero; other statuses use actions 1-4,
except an unrecognized request reports action zero with Invalid/error 1 or Unsupported/error 5.
Results remain until replaced by another action or reboot. There is no request ID, deduplication, or client ownership.
Only one controlling host may write, with serialized operations; other hosts may monitor.
A result is not proof of which client submitted an action.

Confirmation is an explicit host intent flag, not authentication or protection against malicious software.
Standalone reboot requires capability bit 1. `PICO_UART_ALLOW_HID_RESET=0` removes standalone reboot,
not save-and-reboot, which requires capability bit 2. LED actions require capability bit 0.
LED actions are assignments, not toggles. LED starts Off after initialization; all LED access occurs on core 0.
CDC traffic and USB enumeration do not change it. Hosts reread telemetry after a successful LED action.

### Action Validation and Execution

1. While Pending, or Completed awaiting reboot, reject later SET transfers without changing the current result.
   Do not execute or queue another action, including an identical request.
2. Check exact length, version, and globally reserved flag bits. Failure publishes Invalid/error 1.
3. Check action and capability. Failure publishes Unsupported/error 5.
4. Check action-specific fields: LED actions require flags/profile zero; reboot requires profile zero and bit 1 zero.
   Failure publishes Invalid/error 1. Save profile 0 also requires bit 1 zero.
5. For save, validate the defined, firmware-advertised profile request first (Invalid/error 2).
   Require bit 0 for reboot/save (Invalid/error 4), then bit 1 for non-default save (Invalid/error 3).
6. Check readiness, including closed CDC DTR sessions for save (Busy/error 6), then execute or queue.
   Repeat readiness checks at execution. Flash operation/verification failures use errors 7/8; other failures use 9.

Invalid or unsupported requests have no board-control side effects. For an exact-length, version-1 request with
a known action, report that action even on rejection; otherwise report action zero.
Every accepted SET publishes its new result before transfer completion, replacing any prior terminal result.
LED actions may complete immediately; deferred work publishes Pending before execution.
Malformed/unsupported SET requests publish their rejection unless an in-flight operation is protected by step 1.

### Action Timing and Host Rules

- A successful SET transfer is not proof of execution. Read report 20 immediately, then every 50 ms while Pending,
  with a 3000 ms wait budget from successful SET completion. Check the action and terminal status.
- Apply a 3000 ms application deadline to each SET/GET, limiting result reads to the remaining wait budget.
  Use native I/O timeouts when available. WebHID deadlines do not cancel underlying calls: invalidate the local
  operation token, ignore late completions, and never start overlapping writes to bypass a stuck transfer.
- Reboot and save-and-reboot reset 1000 ms after publishing Completed. Reads and later writes do not extend,
  cancel, or postpone it. Completed means ready to reboot, not proof of a successful subsequent boot.
- Timeout/disconnect means unconfirmed, not cancelled or definitely failed. Never automatically retry reboot/save.
  Reconnect and inspect boot configuration. Reconnection alone does not prove a particular request caused reboot.
- Without request IDs, delayed or repeated requests can execute again after the protected operation ends.
  No exactly-once guarantee or multi-client correlation is provided. The host must not replay old writes.

## Reports 10-15: Channel Snapshots

Report `10 + channel` addresses CDC0–CDC5. Publish Input snapshots nominally every **500 ms** while HID IN is ready;
read-only Feature GET returns the same layout on demand. Static pins/capacities belong to report 2; history/peaks
belong to reports 30-35.

Each channel has an independent sequence, byte-counter baseline, and measurement start time. Service due channels
fairly in round-robin order so endpoint contention cannot starve a channel. A busy endpoint delays publication;
do not queue old snapshots or emit catch-up bursts. Sample fresh data when publication can proceed.
The six reports are not an atomic board-wide snapshot, and their sequences or intervals need not match.

### Channel Layout

Order: version, state, applied settings, traffic timing, totals. No padding.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Fresh pre-release layout version `1`. |
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

#### Channel State

| Value | State | Meaning |
| ----: | ----- | ------- |
| `0` | Closed | CDC DTR closed; backend shutdown may still be pending. |
| `1` | Starting | CDC open; initialization or initial configuration not complete. |
| `2` | Ready | CDC open, backend ready, no pending control or current failure. |
| `3` | Configuring | CDC open and backend ready; a line-coding request is pending. |
| `4` | Error | CDC open with a current initialization/control failure; traffic may still progress. |
| `5`-`255` | Unassigned | Invalid in this layout. |

Derive from one coherent control/lifecycle capture in this precedence: Closed, Error, Starting, Configuring, Ready.
Starting includes an open backend not ready even if control is pending. Closed does not hide history in diagnostics
or acknowledge physical stop completion. Missing/stale reports are a host-side Unknown state, not a wire enum.

#### UART Measurement State

| Value | State | Meaning |
| ----: | ----- | ------- |
| `0` | Unavailable | No coherent UART statistics sample exists; UART counters are zero placeholders. |
| `1` | Current | Coherent UART statistics acquired for this capture. |
| `2` | Cached | Last accepted UART statistics reused; UART rates unavailable. |
| `3`-`255` | Unassigned | Invalid in this layout. |

See [UART Measurement Validity](#uart-measurement-validity) for acquisition semantics and diagnostic age.
Error reasons and buffer peaks belong to [Channel Diagnostics](#reports-30-35-channel-diagnostics).

Applied framing fields are meaningful only when baud is nonzero; unavailable framing fields are zero.

| Direction | Counter | Counts |
| --------- | ------- | ------ |
| Host → UART | `cdc_rx_bytes` | Bytes consumed from TinyUSB into UART TX ring. |
| Host → UART | `uart_tx_bytes` | Peripheral/FIFO handoff: HW/PIO DMA completion or PIO polling FIFO writes, not final stop-bit transmission. |
| UART → Host | `uart_rx_bytes` | DMA bytes published into RX ring, including bytes subsequently overwritten. |
| UART → Host | `cdc_tx_bytes` | TinyUSB acceptance, not confirmed host receipt. |

These are handoffs, not delivery acknowledgements. Close/reset/disconnect may discard counted queued bytes.
Counter differences do not measure loss and equality does not prove delivery; use fault counts and payload verification.

### Snapshot Consistency

Capture related per-channel state coherently before encoding either an Input or Feature response.
Baud, framing, and readiness/control state must describe one applied-configuration state, not fields mixed across
a concurrent line-coding update. Within a diagnostic response, counters, last configuration-error reason, and the
shared timestamp must describe one captured fault-history state. Snapshot and diagnostic reads are separate captures: do not enforce cross-report
equality or infer changes happened at the same time.
Read multiword counters without tearing and capture all four traffic counters with the channel's interval boundary.

This does not require an atomic board-wide snapshot or stopping UART traffic. The applied configuration is the
state at capture time; a traffic interval may span several configurations. Capture immutable local values using
the existing ownership/synchronization mechanisms, then encode from that copy rather than rereading live fields.

### Traffic Measurement

Harvest backend counters frequently enough to account for raw 32-bit wrap into wide accumulators; publish low 32 bits.
Totals wrap modulo $2^{32}$, start at MCU boot, and survive USB disconnect/session/dashboard resets.

`interval_ms` measures the time from this channel's preceding accepted Input publication to the current candidate.
Encode `min(elapsed_ms, 65535)`: 1-65534 are usable intervals; 0 and 65535 cannot produce a rate.
After a stall of about 65.5 seconds the rate window is unavailable, but cumulative counters still recover increments
subject to their wrap limits. Do not truncate the interval modulo 65536.
The firmware advances its interval boundary and sequence only when the HID transport accepts publication, not on a
failed/busy attempt. On USB disconnect/reset, restart the publication sequence and interval boundary without clearing
the boot-lifetime traffic counters. Initialize each channel's committed sequence to zero. Encode each candidate Input publication with
`(committed_sequence + 1) modulo 65536`; commit that value only when publication is accepted.
Thus the first accepted Input carries sequence 1; a failed attempt consumes no sequence number.
Feature and control-pipe Input reads return the latest committed sequence, or zero before the first publication,
even though their non-consuming traffic counters may be newer than that publication.
Successful publication is not proof that a host application received the report.
The first Input sample after a host starts or reconnects seeds its displayed total from the current firmware counter
and establishes the rate baseline; it does not produce a rate. Hosts derive each increment as
`(current_counter - previous_counter) modulo 2^32`. Use the device `interval_ms` for a contiguous sample, not nominal
cadence or host arrival time. Zero or saturated intervals cannot produce an exact rate. Show the affected rate as
unavailable. A sequence gap still permits cumulative totals to recover from the next counter, but its rate window
must be discarded because the elapsed device interval for all bytes since the prior host sample is unknown.
Aggregate clean windows using summed byte increments divided by summed device intervals; do not average rates without
weighting. Host arrival time is only for freshness and gap suspicion.

Modulo subtraction is unambiguous only below $2^{32}$ bytes between host samples (about four hours at 3 Mbaud 8N1;
USB counters may wrap sooner). Beyond that, mark derived totals uncertain.
A 16-bit sequence cannot detect loss of exactly 65536 reports. After a read pause/stale stream, rebaseline rates
even if the sequence appears contiguous; retain cumulative counters.
Do not count duplicate or older reports as new traffic while a stream is fresh; a stale stream must still be able
to rebaseline regardless of its next sequence value.
One channel's gap must not invalidate other channels. Sequences are not dashboard values.

Feature GET_REPORT returns current cumulative counters without advancing the Input interval origin, Input rate
baseline, or committed publication sequence. Raw backend accounting is separate: every accepted observation may
advance the raw counter-harvest baseline and update boot-lifetime accumulators exactly once, regardless of whether
background service, Input construction, or a control read acquired it. Repeated observations must not double-count
increments; rejected samples change neither raw accounting nor accepted sample state.
It may refresh fault observations but never consumes an Input rate interval. The same non-consuming behavior
applies to control-pipe GET_REPORT of the Input report. Repeated control reads can overlap and must not be summed or
mixed into interrupt totals. Use the interrupt stream for committed deltas; Feature reads are for on-demand state.

### Configuration Lifecycle

TinyUSB may complete SET_LINE_CODING before validation. Monitor pending, then control error, rather than treating
USB completion as successful application. PIO ports remain 8N1-only. Hardware UART0/UART1 accept firmware-supported
50-3 000 000 baud, 5-8 data bits, 1/2 stop bits, and none/odd/even parity.
Request ordering, timeouts, and retries are defined in the [Control Plane Design](../control-plane-design.md).
For flag transitions/clearing, follow its [request lifecycle](../control-plane-design.md#request-lifecycle),
[generation rules](../control-plane-design.md#generations-and-stale-completions), and
[session reset rules](../control-plane-design.md#usb-mount-and-unmount-reset). An older completion must not clear a
newer error. Routine snapshots summarize lifecycle with `channel_state`; backend identity comes from report 1.
Clearing current failures does not clear retained diagnostic history.

## Reports 30-35: Channel Diagnostics

Read-only Features addressed as `30 + channel`; no Input publication, SET rejected. Read on opening details or
investigating Error; optional bounded low-rate refresh while details are visible, not routine snapshot cadence.
Historical RX faults need not change routine state and have no Input notification. For unattended loss/error alerts,
offer an explicitly enabled, bounded low-rate diagnostic polling mode independent of whether details are visible.
Compare counters within the same boot; distinguish the first observed history from subsequent increments.
There is no boot identifier. Uptime can reveal some resets but cannot prove continuity across a long disconnect.
After reconnect or uncertain boot continuity, establish new comparison baselines and display existing lifetime
history without claiming exact new-fault counts across the gap.
Polling cannot recover event counts beyond saturation or detect faults not captured by the measurement mechanism.

Each GET captures current available diagnostic state without clearing history, consuming traffic, advancing Input
sequences/baselines, or changing transport ownership. UART measurement state and age refer to this diagnostic capture,
not the most recent routine snapshot. Acquiring valid statistics here must preserve boot-lifetime accounting.
There is no cross-report atomicity or publication sequence; the host tracks diagnostic read age separately.

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

See [Channel Measurement Semantics](#channel-measurement-semantics) for validity, reason values, and peak semantics.
On failure, show diagnostics as unavailable/stale rather than replacing them with zeros. A valid routine snapshot
does not refresh old diagnostics; a valid diagnostic read does not restore Input traffic-rate continuity.

| Counter | User-facing label |
| ------- | ----------------- |
| `rx_overrun_count` | Known RX bytes dropped |
| `rx_error_count` | RX controller error observations |

### Compact Fault History

Two saturating `uint32_t` counters retain RX history. The shared timestamp covers observed RX faults and recorded
configuration-error events only; initialization failure alone must not advance it.
No recorded history means all counters zero **and** `last_control_error_reason=0`, and requires timestamp zero.
Every authoritative configuration-error event updates both the last reason and shared timestamp.
A nonzero reason with both RX counts zero is valid.
Recorded history can legitimately have timestamp zero during the first boot second.
The timestamp is uptime, not wall-clock time, and identifies neither
the fault category nor its individual last-seen time. It is rounded down to seconds, with approximately 136 years
of range. Further observed faults refresh it even when their counters have saturated.
Merge valid observations using `max(previous_timestamp, observation_uptime_s)`, saturating at `UINT32_MAX`.
The timestamp must never decrease within a boot; an older UART observation cannot overwrite a newer control fault.
The USB core samples fault observations approximately every 100 ms, even when HID IN is not ready, and when building
routine snapshots or diagnostic responses. On-demand diagnostics must not gate background fault harvesting.
Timestamps describe detection, not exact wire-error time. Counts persist until firmware initialization; backend
counter resets establish a new baseline without subtracting accumulated history.

RX error observations come from sticky backend error flags;
multiple bad frames before harvesting can also coalesce, so these are not per-byte error counts.
Observation/harvesting opportunities can affect coalescing; RX error counts are not directly comparable exact
bad-frame totals across differently monitored runs.
RX overruns include retired drops and known overwritten bytes not yet retired by CDC draining.
At saturation, counts cannot distinguish further events. There is no device-side fault-history clear command;
dashboards show history read-only, without local acknowledgement.

Historical RX indications derive from nonzero counts, are not live signal-level indicators, and survive CDC close.
Floating RX, a break, incorrect baud/framing, and wiring faults are possible causes; diagnostics cannot identify
which occurred. See [Hardware Setup](../../hardware-setup.md).

## Host Polling and Validation

1. Read Feature report 1 using the [identity discovery prefix](#identity-and-capabilities), select the supported
   product/layout, then validate the complete report before sending any board command. PicoUart currently requires
   version 1 and exactly 27 payload bytes. Cache validated static information for the connection, retrying read
   failures without substituting invented identity or capabilities.
2. Read report 2 once per connection for board configuration, channel count, static GPIO assignments, reset cause,
   and shared buffer capacities.
   Validate the count as `6` for PicoUart and use it to interpret report 1's hardware mask; bits above that count
   must be zero. This read does not refresh channel traffic or measurement freshness.
   Submit actions through report 20 only, then read report 20 until the action result is terminal.
   Use one controlling host; there is no per-request correlation.
3. Poll report 3 every 500–1000 ms. Python/.NET dashboards follow the bounded observed Input cadence as documented in
   [host tools](../../development/host-tools.md#board-selection-and-shared-monitoring); other hosts may use a fixed interval
   in that range.
4. Consume Input reports 10-15 for complete channel snapshots and committed traffic deltas. Read their Feature
   counterparts for on-demand state, without counting those overlapping deltas.
5. Read reports 30-35 on demand, or enable bounded low-rate polling explicitly for unattended fault alerts.
   Validate and timestamp independently of snapshots; never treat separate captures as atomic.

Steady state: 1–2 telemetry GETs/second plus nominally 12 channel Input packets/second; no periodic channel Feature
reads. Features can supply initial state without waiting for Input. Read errors invalidate affected live data, not
unrelated static data; valid responses restore it. Mark a channel stale after three seconds without Input.
Features do not restore Input continuity. Disconnect/reboot discards caches and pending-action assumptions;
rediscover identity, capabilities, and configuration.

Validate type, ID, exact length, version, reserved fields/bits, enums, and field consistency.
All sixteen IDs/types are required; unavailable controls use capability flags, not missing reports.
Missing reports/unsupported layouts are explicit errors, not negotiation. Preserve unknown MCU/board IDs.

## Design Validation

Descriptor, firmware, Python, .NET, WebHID, and tests must agree on this contract.
Writes to read-only reports must not execute actions, clear history, or change control state.
Report 20 GETs must not consume results, repeat actions, or change pending work/reset deadlines.
Detailed [regression gates](#shared-decoder-conformance-and-regression-gates) follow.
Physical control/save/reboot testing requires an identified board and explicit authorization.

### Conformance Examples

Hex blocks below are complete selected report-20 requests and responses, excluding the transport report ID.
Assume LED and save capabilities are available, profile 1 is supported, CDC sessions are closed, and the boot
snapshot has no valid stored request (`stored_profile=255`).
Each action example is independent, with no pending operation and one controlling host.

**LED On (report 20):**

```text
01 02 00 00
```

Completed status (report 20):

```text
01 02 02 00
```

**Save profile 1 with reboot confirmation but without overclock acknowledgement (report 20):**

```text
01 04 01 01
```

Invalid/error 3 status (report 20); no flash write or reboot:

```text
01 04 03 03
```

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

## Implementation and Integration

Required invariants for firmware and host implementations. Firmware tests cover production encoding and ownership
paths, and each host client has its own protocol tests. Shared cross-client decoder conformance and physical
qualification remain separate gates.

### Scope and Protocol Policy

The selected unreleased layouts start at version `1`; earlier development layouts are not compatibility targets.
No legacy decoders, migration modes, dual publication, or firmware-version heuristics are required for this
pre-release revision. Update descriptors, firmware, hosts, length checks, and artifact checks together before release;
do not pad unused packet space.

After release, incompatible length/unit/meaning/validation changes require a new version of the affected report,
not merely a firmware semantic-version change. Update this reference with each revised layout and its support policy;
report versions may evolve independently. Hosts must explicitly support a layout before interpreting it.

### Rejected Snapshot Ownership

The UART statistics API builds each attempt in private candidate storage and writes the caller's output only after
sequence validation succeeds. Every failure path leaves the caller's output unchanged, including exhaustion after
candidate fields have been read. Partial candidates must never update HID traffic totals, peaks, or fault history.

HID retains the last accepted UART sample and its acquisition time per channel. CDC counters remain core-0-owned and
may advance while the UART sample is cached. Fault observations derived from UART counters use accepted samples only;
live control/status transitions can still be observed independently.
Never replace a failed sample with zero measurements or advance its acquisition timestamp.

Stopped-backend statistics must be captured coherently after stop completion so final counters and peaks are not
lost between the last active sample and shutdown. A stopped channel can therefore have a valid current sample;
sample validity is not backend readiness. Capture must remain safe if a start/stop occurs concurrently.

### Channel Measurement Semantics

This section owns shared UART acquisition validity and peak interpretation. The snapshot enum is defined in
[UART Measurement State](#uart-measurement-state). These rules apply independently to snapshot and diagnostic
captures; diagnostics never advance Input rate baselines.

#### Last Configuration Error

| Value | Meaning |
| ----: | ------- |
| `0` | No configuration error recorded since firmware initialization. |
| `1` | Unsupported baud setting. |
| `2` | Unsupported framing. |
| `3` | Backend configuration failed. |
| `4` | Control operation timed out. |
| `5` | Other or unknown configuration error. |
| `6`-`255` | Unassigned. |

Update on each authoritative configuration-error event, even while the internal control-error flag remains asserted.
Retain through success, close/reopen, and USB reconnect; clear at firmware initialization.
Carry the reason through existing control ownership
and capture coherently; do not infer it from flags or statistics-read failure. Use Other/unknown for undistinguished
errors. Display **Last configuration error**, not active failure or request correlation.
Each recorded error also updates `last_fault_uptime_s`.

#### UART Measurement Validity

Current describes acquisition, not active transmission or backend readiness. A stopped backend may provide a current
sample. CDC counters remain independently meaningful. Diagnostics reuse
[UART Measurement State](#uart-measurement-state), rather than a separate validity bitmask, and additionally report
UART acquisition age:

| State | Diagnostic age and measurements |
| ----- | ------------------------------- |
| Unavailable (`0`) | Age 255; UART-derived RX counts and ring peaks are zero placeholders, not evidence of fault-free operation. |
| Current (`1`) | Age zero; coherent UART statistics acquired for this capture, including a stopped backend when safely captured. |
| Cached (`2`) | Reuse last accepted UART statistics; age may be zero due to rounding or saturate at 255. |

Age is acquisition age, not packet age; its 100 ms quantization and
25.5-second saturation do not identify exact sample time. Finer/longer age requires a revised layout.
UART validity covers UART traffic, UART ring peaks, and UART-derived RX fault observations. Rejected acquisition must
not advance RX fault counts or the shared fault timestamp from candidate UART data.
Before any accepted UART sample, UART-derived RX counts have no observations; zero does not establish fault-free
operation. CDC counters/peaks and independently captured lifecycle/control state, control-error reason, and control
fault history may update independently. A valid independent fault may advance the shared last-fault timestamp even
while UART measurements are cached. Derive historical RX indicators only from accepted nonzero counts.

#### Observed Buffer Peaks

All peaks are observed maxima in exact byte units, including full 4096-byte rings where captured; exact encoding
does not guarantee that transient occupancy maxima were observed. Firmware must define acquisition points:
UART peaks update at ring producer/consumer accounting boundaries using the occupancy visible there, including
DMA progress when harvested. Do not imply continuously measured DMA occupancy or a lossless record of every peak.
CDC peaks update at defined bridge sampling points. Sample IN occupancy
after writes as well as before draining, where observable; TinyUSB may already have moved bytes into an endpoint.
Do not describe a FIFO peak as total USB in-flight occupancy. Peaks persist across close/reopen and reset only at
firmware initialization. Validate against the current capacities in
[capacity_config.h](../../../firmware/src/config/capacity_config.h) through the capacities advertised in report 2.
Label all peaks **Observed peak since boot**. A full historical peak is neither current congestion nor proof of loss.
Label baud **Applied baud setting**, or **Last applied baud setting** when stopped. It is not a measured wire rate
or a claim of exact divider output.

### Host Information and State Presentation

Packet freshness is not UART freshness. UART rates require contiguous Input sequences and Current UART measurements
at both boundaries. Cached/Unavailable invalidates the UART rate window; the next Current sample rebaselines.
CDC rates remain independent, totals are preserved, and Features do not establish rate baselines.

Display Unknown for a missing/stale snapshot; otherwise display its `channel_state`, with Error presented as Attention
and details obtained from diagnostics. Use the documented wire-state precedence rather than deriving a competing
summary from a separately captured diagnostic response.

Historical faults remain visible beside the lifecycle state without making every recovered channel appear actively
failed. Do not label a stopped receiver "enabled". Show applied settings as last applied when the backend is stopped.
Use the board metric labels and freshness rules in [Board Telemetry](#report-id-3-board-telemetry).
Display static RAM accounting as described in [Firmware RAM Reservation](#firmware-ram-reservation); a telemetry
failure does not invalidate independently cached static RAM data.

### Shared Decoder Conformance and Regression Gates

Python, .NET, and WebHID must consume one repository-owned binary-vector fixture: report ID/type, payload hex,
identity context, validity, and normalized expected fields. Reject invalid cases in every decoder; no silent skips.
Vectors must not be generated by the decoders under test.

| Gate | Required coverage |
| ---- | ----------------- |
| Wire contract | Every table's exact offsets, lengths, types, endian order, enums, reserved bits, and sentinels agree across descriptors, firmware, hosts, and binary artifact checks. No static pins in snapshots or reserved RAM in telemetry. |
| Identity/configuration | Discovery with a short/missing prefix, unknown products, unsupported versions, and other-product identity lengths; no product-specific decoding or commands before complete validation. Unknown MCU/board IDs, profile sentinels, evidenced reset causes versus Unknown, software-requested/watchdog distinction, advertised capacity/peak boundaries and missing-capacity presentation. No MCU-based capacity guesses. |
| UART acquisition | Failure after candidate population leaves output, cache, totals, and RX fault history unchanged; recovery after repeated failure accounts exactly without false wrap. Independent control observations may still advance. |
| Lifecycle | Every state/precedence and measurement state, including coherent stopped samples; close/reopen retains final lifetime counters/peaks. Unavailable is not measured zero. |
| Traffic | Fair scheduling, first-sample/failed-publication accounting, independent sequence/baselines, raw counter wrap, and non-consuming control reads. Alternate background/Input/Feature harvesting of the same increments: account once while preserving Input interval/rate baselines on control reads. Test intervals 0, 1, 65534, 65535, and longer stalls: saturate, never wrap. Cached UART invalidates only its rate window. |
| Diagnostics | Shared measurement enum, Current age zero, Unavailable age 255, Cached age zero/saturation, byte-exact observed peaks at documented acquisition points, and RX history derived from counts. Reason-only history with zero RX counts is valid; test monotonic timestamps with out-of-order observations, first-second zeros, repeated errors, retention, and saturation. Initialization failure alone does not advance the timestamp. Background harvesting needs no reader. |
| Cross-report independence | Separate captures may differ; diagnostic GETs preserve history/Input baselines and do not refresh snapshot age, nor vice versa. |
| Framing | Supported baud/data/stop/parity bounds and PIO 8N1 restrictions. |
| Actions | Valid/invalid status-error pairs, confirmation/argument rules, validation precedence, serialization, protected results, reset deadlines, and flash safety. Publish results before SET completion; reject read-only writes without side effects. No destructive replay after timeout/disconnect or late completion. |
| Presentation | Mandatory telemetry acquisition failure rejects the entire GET without a successful partial/zero-filled response; recovery restores live values. Load warm-up/absent LED remain valid sentinel cases. Service-load naming, retained independent static RAM, no requested-clock substitution, separate lifecycle/history, honest counter units/observed-peak labels, and optional bounded unattended polling. Reconnect/uncertain boot continuity starts new comparison baselines without claiming cross-gap fault increments. |

No physical qualification is implied by these gates. Actual USB/DMA concurrency still requires authorized HIL.

This reference owns wire layouts and field meanings. The [CDC/HID Overview](cdc-hid-overview.md) defines interface
roles; the [Control Plane Design](../control-plane-design.md) defines deferred UART control ownership.
Current client entry points are documented in the [Python host guide](../../../host/python/README.md),
[.NET host guide](../../../host/dotnet/README.md), and
[Host Tools Development](../../development/host-tools.md).

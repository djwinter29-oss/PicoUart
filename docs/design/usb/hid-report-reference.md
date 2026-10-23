# HID Report Reference

This document defines the version-1 HID design: static device information, board controls, live telemetry,
and combined per-channel snapshots.

PicoUart exposes one vendor-defined USB HID interface named `Status Monitor`. HID provides device information,
board controls, and diagnostics for the six UART bridges. UART data and line configuration remain on the matching
USB CDC interface. All UART receivers start at board boot and bridge data whenever their matching CDC interface
is connected. DTR state is reported for monitoring and checked for configuration-save safety.

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
| Board identification LED | HID report 2 | Explicit On/Off commands when an LED is available. |
| Standalone board reboot | HID report 2 | Capability-gated arm/reboot sequence within 2 seconds. |
| Save boot configuration and reboot | HID report 2 | Validated clock profile, overclock acknowledgement, closed CDC sessions, verified flash write, then automatic reboot. |

HID does not select or reconfigure a UART, change GPIO mapping, or alter ring-buffer behavior.
Reading a channel's diagnostic report does not change transport ownership.

## Report IDs

The first page contains static device information, followed by board controls, live telemetry, traffic, and detailed
channel diagnostics. These are logical report groups, not separate HID usage pages or mandatory dashboard tabs.

| Report ID | Type | Direction | Payload | Purpose |
| --------- | ---- | --------- | ------- | ------- |
| `1` | Feature | Host reads | 63 bytes | Static device identity, firmware version, MCU, capabilities, and total RAM. |
| `2` | Feature | Host reads/writes | 24 bytes | Board commands, command result, and boot configuration. |
| `3` | Feature | Host reads | 24 bytes | Board temperature, LED state, uptime, service load, reserved RAM, and current clock. |
| `10`-`15` | Input / Feature | Device publishes / host reads | 54 bytes each | One combined snapshot per channel: traffic, health, applied settings, GPIOs, and compact fault history. |

There are nine unique report IDs. IDs `4`-`9` and `16`-`255` are unassigned.
Report `10 + channel` directly addresses a channel without a mutable selector. Each channel ID defines both an
Input report and a read-only Feature report with the same layout; report type plus ID identifies the operation.

All multi-byte integers are little-endian. Report ID bytes belong to the HID transport and are excluded from the
payload offsets and lengths below. Report ID plus payload fits within 64 bytes for every report.
All report payloads use layout version `1`. Layout versions are independent of the firmware semantic version.

## Report ID 1: Device Information

Read once per connection. All fields are static until a reboot and USB re-enumeration.
Live temperature, LED state, current clock, and command results do not belong on this page.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 32 | `product_name` | Printable ASCII USB product string, nonempty, NUL-terminated and zero-padded. |
| 33 | 16 | `serial_number` | Sixteen hexadecimal ASCII characters, no terminator. |
| 49 | 1 | `firmware_major` | Firmware semantic version major component. |
| 50 | 1 | `firmware_minor` | Firmware semantic version minor component. |
| 51 | 1 | `firmware_patch` | Firmware semantic version patch component. |
| 52 | 1 | `mcu` | `1` = RP2040; `2` = RP2350. |
| 53 | 1 | `channel_count` | `6`. |
| 54 | 1 | `hardware_channel_mask` | Bit `n` identifies a hardware UART channel; currently `0x03`. |
| 55 | 1 | `pio_channel_mask` | Bit `n` identifies a PIO 8N1 channel; currently `0x3c`. |
| 56 | 2 | `capabilities` | Static board-control capability flags. |
| 58 | 1 | `clock_profile_mask` | Bit `n` advertises supported boot clock profile `n`; see report 2. |
| 59 | 4 | `ram_total_bytes` | Total SRAM for the selected MCU/linker layout; nonzero. |

### Identity and Capabilities

Display `serial_number` as **Serial / UID**. It is the same SDK unique board ID used for the USB serial descriptor,
not a second independent identity or the numeric MCU type. On RP2040 it derives from the external QSPI flash unique
ID, not a silicon serial. The SDK supplies the platform-specific identity on RP2350. Do not substitute host-generated
or browser-session identifiers.

MCU type identifies silicon, not the board vendor or model. Hosts retain unknown numeric MCU IDs and display
`Unknown MCU (ID)` rather than discarding otherwise valid information.

Tag `v1.2.3` builds advertise firmware `1.2.3`. USB descriptor `bcdDevice` still carries only major.minor as BCD.
Channel masks must be disjoint, contain no bits above channel 5, and together cover all six channels.

| Capability bit | Meaning |
| -------------: | ------- |
| 0 | Board identification LED available. |
| 1 | Standalone arm/reboot enabled by `PICO_UART_ALLOW_HID_RESET`. |
| 2 | Boot-configuration save-and-reboot available. |
| 3-15 | Reserved, zero. |

Clock-profile mask bits 0-4 describe target-supported profiles; bits 5-7 are reserved and zero.
Bit 0 must be set. A supported non-default profile is not a claim of safe overclock operation on every board.

## Report ID 2: Board Control and Boot Configuration

SET submits a board-scoped command; GET returns its result and the configuration selected at boot.
The report is not arbitrary flash access. All requests use exactly 24 payload bytes.

| Offset | Size | SET_REPORT field | GET_REPORT field |
| -----: | ---: | ---------------- | ---------------- |
| 0 | 1 | `version = 1` | `version = 1` |
| 1 | 1 | `command` | Command associated with the result, or `0` when idle/unidentifiable. |
| 2 | 1 | `flags` | Flags associated with the result, or zero when idle/unidentifiable. |
| 3 | 1 | Zero | `status` |
| 4 | 4 | Nonzero `request_id` | Matching request ID, or zero when idle/unidentifiable. |
| 8 | 4 | Zero | `stored_profile`: accepted profile at boot, or `0xffffffff` unavailable. |
| 12 | 4 | Zero | `active_profile`: profile selected for this boot. |
| 16 | 4 | `requested_profile`: save command only; otherwise zero. | Requested profile, or `0xffffffff` when not applicable/invalid. |
| 20 | 1 | Zero | Configuration `source`. |
| 21 | 1 | Zero | Configuration `reason`. |
| 22 | 1 | Zero | `error_reason`, independent of boot configuration `reason`. |
| 23 | 1 | Reserved, zero | Reserved, zero. |

### Commands and Results

| Command | Name | Behavior |
| ------: | ---- | -------- |
| `1` | LED Off | Set identification LED Off. |
| `2` | LED On | Set identification LED On. |
| `3` | Arm reboot | Authorize a standalone reboot for 2 seconds. |
| `4` | Reboot | Consume valid arm authorization and schedule a watchdog reboot after the result-read grace period. |
| `5` | Save configuration and reboot | Validate and save the requested clock profile, then automatically reboot after the result-read grace period. |

LED commands are explicit assignments, not toggles, so retrying does not invert the desired state.
CDC traffic and USB enumeration do not change the LED. LED state starts Off after firmware initialization;
all LED access occurs on core 0. Hosts reread telemetry after a successful LED command.

Flags bit 0 acknowledges unsafe overclocking and is required for non-default save requests.
All other flag bits are zero; non-save commands require flags zero.

| Status | Name | Meaning |
| -----: | ---- | ------- |
| `0` | Idle | No command result yet. |
| `1` | Pending | Accepted, not yet complete. |
| `2` | Completed | LED/arm action completed, or reboot scheduled; a save has passed flash verification. |
| `3` | Invalid | Malformed request, invalid profile, missing acknowledgement, or expired/missing reboot authorization. |
| `4` | Unsupported | Unknown command or required capability unavailable. |
| `5` | Busy | Save rejected because a CDC DTR session is open. |
| `6` | Failed | Execution failed, including flash write/verification failure. |

| Error reason | Name | Required status |
| -----------: | ---- | --------------- |
| `0` | None | Idle, Pending, or Completed |
| `1` | Malformed payload: version, length, flags, reserved fields, or request ID | Invalid |
| `2` | Invalid or target-unsupported clock profile | Invalid |
| `3` | Missing overclock acknowledgement | Invalid |
| `4` | Missing or expired reboot authorization | Invalid |
| `5` | Unknown command or unavailable capability | Unsupported |
| `6` | CDC DTR session open | Busy |
| `7` | Flash operation failed | Failed |
| `8` | Flash verification failed | Failed |
| `9` | Other execution failure | Failed |

Hosts validate status/error-reason consistency. Error reason describes the command result, not boot recovery.

A successful USB SET transfer is not proof of execution. Hosts check command, request ID, requested profile where
applicable, and terminal status. For rebooting commands, Completed means ready to reboot, not proof of a subsequent
successful boot. Reconnect and read configuration to verify the resulting boot.

Only one request/result slot exists. Hosts must serialize control operations; request IDs correlate results but
are not authentication or multi-client isolation. An identifiable rejected request publishes its error result.
A request is identifiable for result correlation only when its payload is exactly 24 bytes, its version is 1, and
its request ID is nonzero. Otherwise publish Invalid/error 1 with command/flags/request ID zero and requested profile
unavailable. Invalid or unsupported requests have no board-control side effects.

For identifiable requests, echo command, flags, and request ID verbatim, including in rejection results.
Echo the requested profile only for command 5 when the profile is target-supported and argument validation has
succeeded through profile validation; otherwise return `0xffffffff`. Missing acknowledgement can therefore return
the valid requested profile. Never echo request bytes into the response version, reserved byte, boot configuration,
status, or error reason.

Hosts validate rejection responses differently from accepted commands: unknown command values are permitted with
Unsupported/error 5 or Invalid/error 1. Globally undefined flag bits 1-7 are permitted only with Invalid/error 1;
globally defined bit 0 may be echoed with Unsupported/error 5 before command-specific validation has run.
These echoed fields describe rejected input, not new supported commands or capabilities. Response version, length,
reserved byte, boot metadata, and status/error pairing must always satisfy this contract.

While an operation is Pending, and after a rebooting operation is Completed awaiting reboot, later SET requests
leave the current operation and result unchanged, including malformed requests. A host that finds a different request
ID must treat its own request as unconfirmed, not successful. Results remain until replaced by another request or reboot.
An exact retry of the most recent request returns the existing result without repeating its side effects or extending
an arm window. Reuse of that request ID with different contents is not a new command and leaves the result unchanged.
Hosts use a new nonzero ID for each new operation and must not automatically retry an unconfirmed reboot/save.
Before allocating an ID, read the latest control result and choose a nonzero 32-bit ID different from that result
and every ID already used by this controlling client during the connection. A simple recommended allocator starts
at 1, advances monotonically, and skips the latest device ID. Retain only the next candidate, not a set of used IDs;
advance it when allocating even if the operation fails or times out. Do not wrap within a client connection:
if advancing or skipping would exceed `UINT32_MAX`, end the controlling-client connection and re-establish state
rather than reuse an exhausted ID space. Prefer one controlling client with serialized writes; other clients may monitor.
Reading the latest ID does not reserve the slot, so concurrent clients can still collide and are not isolated.
Deduplication remembers only the latest request, not a history. After another request replaces that slot, a delayed
retry of an older request can execute again; there is no general exactly-once guarantee.

Standalone arm/reboot requires capability bit 1. USB disconnect revokes arm authorization; reboot consumes it.
The `PICO_UART_ALLOW_HID_RESET=0` build option removes standalone reboot, not configuration-save reboot.
Completed for an arm command records a past action, not the current armed state. Its result can remain Completed
after expiry or disconnect-time revocation. A duplicate arm request does not restore authorization; use a new
request ID and a fresh arm/reboot sequence when authorization is no longer known to be valid.

### Request Validation Order

Apply the following order so implementations agree when a request has multiple faults:

1. Preserve the current operation/result while Pending or Completed awaiting reboot, without interpreting a new request.
2. For an exact-size request with the supported version and a nonzero request ID matching the latest request,
   apply the deduplication rules before replacing any result, including when its other contents differ.
3. Validate framing: length, version, request ID, unconditional reserved/zero fields, and globally defined flag bits.
   Bits 1-7 are always invalid; bit 0 is globally defined. Report Invalid, error reason 1, on failure.
4. Validate command and capability. Report Unsupported, error reason 5, on failure.
5. For a supported command with its capability available, validate command-specific zero requirements:
   commands 1-4 require flags and requested profile zero. Violations are Invalid/error 1.
6. Validate command arguments: profile first (error 2), then required overclock acknowledgement (error 3),
   then reboot authorization where applicable (error 4). These failures are Invalid.
7. Check execution readiness, including CDC DTR checks for saves (Busy, error 6), then execute or queue the command.
   Repeat required readiness checks at execution; report flash operation/verification failures as errors 7/8
   and other execution failures as error 9.

Profile validation and overclock acknowledgement apply only to saves. An otherwise well-framed unknown command
with flags bit 0 set returns Unsupported/error 5; an unknown command with bit 7 set returns Invalid/error 1 first.
A missing required acknowledgement is a command-argument error, not malformed framing.

### Command Timing

- Arm authorization expires 2000 ms after the arm command completes. Reboot must be accepted before expiry.
- Reboot and save-and-reboot schedule reset 1000 ms after publishing Completed. Reads and retries do not extend it.
- Hosts read results immediately after SET, then every 50 ms while awaiting completion, with a 3000 ms wait budget
  measured from successful SET completion. Also apply a 3000 ms application deadline to each SET/GET transfer,
  limiting result reads to the remaining wait budget. Use native I/O timeouts where the transport supports them.
- An application deadline does not imply cancellation of the underlying transfer. WebHID has no per-transfer timeout
  or cancellation API. After a deadline, stop the command sequence, invalidate its operation token, and ignore late
  completions for that operation or an earlier connection. Do not start overlapping commands to bypass a stuck call.
- A host timeout or disconnect is an unconfirmed outcome, not cancellation or proof of failure. Do not automatically
  resubmit reboot/save; reconnect and inspect boot configuration. A timed-out operation may still complete.

### Boot Configuration

Configuration source: defaults=`0`, stored=`1`, recovery=`2`.
Reason: none=`0`, erased=`1`, invalid_header=`2`, crc_failed=`3`, unsupported_version=`4`, invalid_profile=`5`,
watchdog_recovery=`6`, clock_unavailable=`7`.
Source stored requires reason none and matching stored/active profiles, including profile 0.
Recovery requires a nonzero reason. Invalid storage selects defaults in RAM without automatically rewriting flash.

| Profile | Clock | Targets |
| ------: | ----- | ------- |
| `0` | SDK default; no clock reconfiguration | Both |
| `1` | 150 MHz | Both |
| `2` | 200 MHz | Both |
| `3` | 250 MHz | Both |
| `4` | 280 MHz | RP2350 only |

Firmware settings remain a private cache accessed through `boot_config_get`, `boot_config_set`, and `boot_config_save`.
The same profile IDs are used by settings, storage, and HID. Target detection and validation stay within that module.
Watchdog recovery or clock-application failure can bypass a stored request without changing it.

### Save and Reboot Safety

Accept only an advertised profile with the required acknowledgement. Missing acknowledgement, unknown profiles,
wrong-family profiles, invalid versions, lengths, reserved fields, or flags must not write flash.
Every CDC DTR session must be closed both at acceptance and execution.
Stop external UART traffic too: receivers remain active and flash erase pauses CPU servicing, so traffic may be
lost even with DTR clear.

Queue the operation for the USB poll loop, outside the TinyUSB control callback.
The SDK flash-safe execution API disables interrupts and locks out core 1, registered as a lockout victim during
UART worker initialization. Erase only the reserved final sector; its first page contains the CRC-protected record
and the remainder stays erased. Full-sector validation and page comparison must succeed before reporting Completed.
Saving an already valid identical profile does not erase/program again.

The configuration source/reason and stored/active profile snapshot remain unchanged until reboot.
`requested_profile` identifies the newly verified save result. Reading that result does not trigger, delay, or
prevent the scheduled reboot. Later requests cannot replace the result, rewrite flash, cancel, or postpone reboot.
Interrupted writes fall back to SDK defaults, not a previous record: storage is a single sector, not a redundant journal.
Non-default profiles remain experimental and require exact-board HIL; save success does not establish stability.

## Report ID 3: Board Telemetry

One read supplies live board monitoring values. Static identity/capabilities belong only in report 1;
boot configuration and command results belong only in report 2.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 1 | `led_flags` | Bit 0: identification output On. Other bits zero. |
| 2 | 2 | `temperature_centidegrees_celsius` | Signed internal RP2 sensor estimate, in hundredths of a degree Celsius. |
| 4 | 8 | `uptime_ms` | SDK monotonic milliseconds since boot. |
| 12 | 2 | `core0_service_load_basis_points` | `10000` = 100%; `65535` unknown. |
| 14 | 2 | `core1_service_load_basis_points` | Same scale and sentinel. |
| 16 | 4 | `ram_reserved_bytes` | Static and reserved SRAM, not live heap/stack use. |
| 20 | 4 | `system_clock_hz` | Current SDK-reported `clock_get_hz(clk_sys)`, nonzero. |

Total RAM comes from report 1. `ram_reserved_bytes` is also fixed for a firmware build, but remains here because
the device-information payload is full; it is not a live memory-use measurement.

An unavailable LED has flags zero, but hosts display it as unavailable using report 1's capability, not as Off.
A failed telemetry read clears live values and disables LED controls until valid telemetry returns.
System clock reflects configured frequency, not an independently calibrated oscillator measurement.

Channel overflow counts come from each channel snapshot's `rx_overrun_count`; board telemetry does not duplicate
them. Combined channel publication requires no diagnostics-generation counters or diagnostic-cache expiry policy.

### Runtime Interpretation

Service load is elapsed wall time of service passes that handle work divided by actual measurement-window duration,
in basis points (0.01%). Each core publishes after at least one second; before its first window it reports `65535`.
Empty polling passes are excluded. Core 0 counts USB events, CDC transfers, control progress, and HID publication;
core 1 counts UART data progress, control handling, and error recovery. Waiting for DMA alone does not count as work.

The complete marked pass is charged once, including polling overhead and interrupts during it.
This is a workload proxy, not total CPU utilization, instruction profiling, line occupancy, or calibrated remaining
throughput. Both cores still busy-poll. Monitoring and noisy RX/error handling can create nonzero load without
application traffic. Host JSON exposes these values as `cpu_usage_basis_points`.

Reject loads above `10000` except the unknown sentinel, zero total RAM, or reservations exceeding total.
Uptime is device uptime, not monitoring-session duration. Display RAM as reserved / total KiB and a percentage,
not heap/stack high-water marks. Reservation includes linked SRAM from `SRAM_BASE` through `__end__`, scratch X/Y
linked sections, and both SDK stack reservations. It excludes remaining heap capacity and does not measure live
allocations or stack use. Total SRAM is `SRAM_END - SRAM_BASE`, including scratch banks.

## Reports 10-15: Channel Snapshots

Report `10 + channel` identifies CDC0/UART0 through CDC5/UART5. Each channel publishes one combined Input snapshot
at a nominal **500 ms** interval while HID IN is ready. The same 54-byte payload is available through read-only
Feature GET_REPORT. Including the report ID, each report is 55 bytes and fits in one packet.

The report version byte remains `1`. In this v1 contract, the four traffic fields are cumulative boot-lifetime counters,
not the per-interval deltas used by earlier v1 firmware. Older host builds will misinterpret them; upgrade firmware and
host software together.

Each channel has an independent sequence, byte-counter baseline, and measurement start time. Service due channels
fairly in round-robin order so endpoint contention cannot starve a channel. A busy endpoint delays publication;
do not queue old snapshots or emit catch-up bursts. Sample fresh data when publication can proceed.
The six reports are not an atomic board-wide snapshot, and their sequences or intervals need not match.

| Offset | Size | Field | Meaning |
| -----: | ---: | ----- | ------- |
| 0 | 1 | `version` | Layout version, `1`. |
| 1 | 2 | `sequence` | Per-channel publication sequence, modulo 65536. |
| 3 | 4 | `interval_ms` | Elapsed milliseconds since this channel's preceding accepted Input publication; saturates at `UINT32_MAX`. |
| 7 | 1 | `health` | Driver, CDC-open, backend, and historical RX flags. |
| 8 | 1 | `ring_high_watermark_blocks` | Largest RX/TX ring occupancy, rounded up to 16-byte blocks; saturates at 255 (4080 bytes or more). |
| 9 | 4 | `uart_tx_bytes` | Low 32 bits of UART TX bytes since MCU boot; wraps modulo $2^{32}$. |
| 13 | 4 | `uart_rx_bytes` | Low 32 bits of valid UART RX bytes since MCU boot; wraps modulo $2^{32}$. |
| 17 | 4 | `cdc_tx_bytes` | Low 32 bits of device-to-host CDC bytes since MCU boot; wraps modulo $2^{32}$. |
| 21 | 4 | `cdc_rx_bytes` | Low 32 bits of host-to-device CDC bytes since MCU boot; wraps modulo $2^{32}$. |
| 25 | 4 | `baud_rate` | Successfully applied baud rate, not last requested; zero if unavailable. |
| 29 | 1 | `data_bits` | Successfully applied data-bit count. |
| 30 | 1 | `stop_bits` | Successfully applied stop-bit count, 1 or 2. |
| 31 | 1 | `parity` | None=`0`, odd=`1`, even=`2`. |
| 32 | 1 | `tx_pin` | Board-configured TX GPIO; 255 means unassigned. |
| 33 | 1 | `rx_pin` | Board-configured RX GPIO; 255 means unassigned. |
| 34 | 4 | `init_failed_count` | Saturating count of observed initialization-failure transitions. |
| 38 | 4 | `control_error_count` | Saturating count of observed control-error transitions. |
| 42 | 4 | `rx_overrun_count` | Saturating cumulative dropped UART RX bytes. |
| 46 | 4 | `rx_error_count` | Saturating count of controller error observations. |
| 50 | 4 | `last_fault_uptime_s` | Latest observation of any fault, in boot uptime seconds; saturates at `UINT32_MAX`. |

No signature or channel-number field is needed: report type, ID, length, and version identify the payload.
Applied framing fields are meaningful only when baud is nonzero; unavailable framing fields are zero.

Traffic counters measure handoffs, not end-to-end delivery acknowledgements. `cdc_rx_bytes` counts bytes consumed from
TinyUSB into the UART TX ring; `cdc_tx_bytes` counts UART RX bytes accepted by TinyUSB, not confirmed host receipt.
`uart_tx_bytes` counts bytes handed to the UART peripheral/FIFO: hardware UART and PIO DMA paths account at DMA
completion, while PIO polling accounts at FIFO writes. Neither means the last stop bit has left the TX pin.
`uart_rx_bytes` counts DMA-received bytes published into the RX ring, including bytes subsequently lost to ring overflow.
Close, reset, or disconnect can discard queued bytes after a handoff was counted. Use payload verification and fault
counters alongside traffic counters; equality between counters is not a delivery guarantee.

### Snapshot Consistency

Capture related per-channel state coherently before encoding either an Input or Feature response.
Baud, framing, and readiness/control health must describe one applied-configuration state, not fields mixed across
a concurrent line-coding update. Fault counters, historical flags, and the shared timestamp must agree with the
same captured fault-history state; for example, a nonzero RX-overrun count requires its historical health flag.
Read multiword counters without tearing and capture all four traffic counters with the channel's interval boundary.

This does not require an atomic board-wide snapshot or stopping UART traffic. The applied configuration is the
state at capture time; a traffic interval may span several configurations. Capture immutable local values using
the existing ownership/synchronization mechanisms, then encode from that copy rather than rereading live fields.

### Traffic Measurement

The firmware harvests wrapping backend counters into wide accumulators and publishes their low 32 bits. These
counters start at MCU boot and survive USB disconnects, host-session resets, and dashboard reconnects; an MCU reboot
starts new counters. They wrap modulo $2^{32}$ rather than saturating. The underlying counters are sampled frequently
enough to account for raw 32-bit backend-counter wrap.

`interval_ms` measures the time from this channel's preceding accepted Input publication to the current candidate.
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

Modulo subtraction is unambiguous only when fewer than $2^{32}$ bytes pass between host samples. At 3 Mbaud, 8N1,
a continuously active UART direction wraps in about four hours; USB-side counters may wrap sooner. If a host is paused
long enough for multiple wraps, mark its derived totals uncertain. Ring peak remains cumulative from boot and
saturates at 4080 bytes.

Track sequences separately per channel, expecting an increment modulo 65536. A sequence gap resets the affected rate
window, but cumulative counters recover total bytes across missing reports as long as fewer than $2^{32}$ bytes pass
between host samples. A 16-bit sequence cannot detect a loss of exactly 65536 reports; after a host read pause or
stale stream, conservatively mark rate continuity uncertain even if the next sequence matches. Re-establish the rate
baseline from the next Input sample after staleness while retaining its current cumulative counters.
Do not count duplicate or older reports as new traffic while a stream is fresh; a stale stream must still be able
to rebaseline regardless of its next sequence value.
One channel's gap must not invalidate other channels. Sequences are not dashboard values.

Feature GET_REPORT returns current cumulative counters without advancing interval origin, counter-harvest baseline,
or sequence. It may refresh fault observations but never consumes an Input rate interval. The same non-consuming behavior
applies to control-pipe GET_REPORT of the Input report. Repeated control reads can overlap and must not be summed or
mixed into interrupt totals. Use the interrupt stream for committed deltas; Feature reads are for on-demand state.

### Health Bits

| Bit | Meaning |
| --: | ------- |
| 0 | Backend initialized and ready. |
| 1 | Backend initialization failed. |
| 2 | Most recent control request failed: invalid/unsupported line coding, deferred-apply timeout, or backend reject. |
| 3 | Line-coding control request pending. |
| 4 | Matching CDC DTR session open. |
| 5 | PIO backend. |
| 6 | Historical RX overrun observed. |
| 7 | Historical RX error observed. |

TinyUSB may complete SET_LINE_CODING before validation. Monitor pending, then control error, rather than treating
USB completion as successful application. PIO ports remain 8N1-only. Hardware UART0/UART1 accept firmware-supported
50-3 000 000 baud, 5-8 data bits, 1/2 stop bits, and none/odd/even parity.
Request ordering, timeouts, and retries are defined in the [Control Plane Design](../control-plane-design.md).

### Compact Fault History

Four saturating `uint32_t` counters retain per-category history, with one shared last-fault timestamp.
All-zero counters mean no observed faults and require timestamp zero. A nonzero count can legitimately have
timestamp zero during the first boot second. The timestamp is uptime, not wall-clock time, and identifies neither
the fault category nor its individual last-seen time. It is rounded down to seconds, with approximately 136 years
of range. Further observed faults refresh it even when their counters have saturated.
The USB core samples fault observations approximately every 100 ms, even when HID IN is not ready, and when building
channel snapshots for publication or control reads.
Timestamps describe detection, not exact wire-error time. Counts persist until firmware initialization; backend
counter resets establish a new baseline without subtracting accumulated history.

`init_failed_count` and `control_error_count` count observed deasserted-to-asserted transitions, not individual failures.
Multiple transitions between samples can coalesce. RX error observations come from sticky backend error flags;
multiple bad frames before harvesting can also coalesce, so these are not per-byte error counts.
RX overruns include retired drops and known overwritten bytes not yet retired by CDC draining.
At saturation, counts cannot distinguish further events. There is no device-side fault-history clear command;
dashboards show history read-only, without local acknowledgement.

Historical RX flags are not live signal-level indicators. They survive CDC close even though closing stops the backend.
Floating RX, a break, incorrect baud/framing, and wiring faults are possible causes; diagnostics cannot identify
which occurred. See [Hardware Setup](../../hardware-setup.md).

## Host Polling and Validation

1. Read Feature report 1 and validate layout version 1 before sending any board command. Cache static information
   for the connection, retrying failures without substituting invented identity or capabilities.
2. Read report 2 at connection for boot configuration. Read it around commands until the matching result is terminal.
3. Poll report 3 every 500–1000 ms. Python/.NET dashboards follow the bounded observed Input cadence as documented in
   [host tools](../../development/host-tools.md#board-selection-and-shared-monitoring); other hosts may use a fixed interval
   in that range.
4. Consume Input reports 10-15 for complete channel snapshots and committed traffic deltas. Read their Feature
   counterparts for on-demand state, without counting those overlapping deltas.

Steady-state monitoring requires one or two board-telemetry control reads per second when no command is active, plus six
channel Input publications per 500 ms (nominally twelve packets per second). No periodic channel Feature reads
are required. A channel Feature read can obtain initial state without waiting for the stream.
Read failures clear affected live values, not unrelated valid static data. Failed channel reads surface an error;
later valid channel snapshots restore their values. After three seconds without an Input sample, mark that channel
stale; Feature reads must not refresh interrupt-traffic continuity. After disconnect/reboot, discard cached values
and pending command assumptions; rediscover identity, capabilities, and active configuration.

Validate report type, ID, payload length, version, reserved fields/bits, enums, and field consistency.
All nine IDs and their listed report types are required; unavailable board controls are expressed by capability flags,
not missing reports.
Unsupported layouts or missing required reports are explicit errors, not compatibility negotiation.
No report carries a signature field. Preserve unknown MCU IDs as described above.

## Design Validation

The HID descriptor, firmware, Python and .NET clients, WebHID client, and associated tests must use the same contract.
Channel reports 10-15 define Input and read-only Feature layouts; Feature writes must not execute board commands.
Verify exact sizes, parsing and error paths, static caching, shared fault-timestamp semantics, traffic delta semantics,
independent channel sequences/baselines and fair scheduling, non-consuming Feature reads, first-sample accounting,
internal counter-wrap handling, command correlation/retries, validation precedence,
expired arm results, late host completions, reset authorization, and flash-save safety.
Physical control/save/reboot validation requires an identified board and explicit user authorization.

### Conformance Examples

Hex blocks below are complete 24-byte report-2 payloads, excluding the transport report ID.
Assume LED and save capabilities are available, profile 1 is supported, CDC sessions are closed, and the boot
snapshot is defaults/erased (`source=0`, `reason=1`, stored unavailable, active profile 0).
Each command example is independent, with no pending operation or conflicting latest request ID.

**LED On, request ID 1:**

```text
01 02 00 00  01 00 00 00  00 00 00 00
00 00 00 00  00 00 00 00  00 00 00 00
```

Matching Completed response:

```text
01 02 00 02  01 00 00 00  ff ff ff ff
00 00 00 00  ff ff ff ff  00 01 00 00
```

**Save profile 1 without overclock acknowledgement, request ID 2:**

```text
01 05 00 00  02 00 00 00  00 00 00 00
00 00 00 00  01 00 00 00  00 00 00 00
```

Matching Invalid/error 3 response; no flash write or reboot:

```text
01 05 00 03  02 00 00 00  ff ff ff ff
00 00 00 00  01 00 00 00  00 01 03 00
```

| Scenario | Required behavior |
| -------- | ----------------- |
| Identifiable LED request with flags `0x80` | Return Invalid/error 1, echo command/flags/request ID, leave LED unchanged, and report requested profile unavailable. |
| Unknown command `0x7f` with flags `0x01`, otherwise well-framed | Return Unsupported/error 5, echo command/flags/request ID, and report requested profile unavailable. |
| Unknown command `0x7f` with flags `0x80`, otherwise well-framed | Return Invalid/error 1 before command validation; echo command/flags/request ID. |
| First channel publication fails, a Feature read follows, then publication succeeds | Failed candidate uses sequence 1 without committing; Feature returns 0; first accepted Input returns 1. |
| Channel sequence `65535` followed by `0` | Normal modulo-65536 continuity, not a gap. |
| Input at time T; Feature read at T+250 ms; next Input at T+500 ms | Feature read does not commit a baseline. Next Input still covers 500 ms from T and includes all interval bytes once. |
| Channel 0 sequence jumps from 10 to 12 while channel 1 advances normally | Recover channel 0's cumulative totals, discard its rate window, and leave channel 1 unaffected. |
| Line coding changes during snapshot capture | Report either coherent old or coherent new applied configuration, never a mixture. |

This reference owns wire layouts and field meanings. The [CDC/HID Overview](cdc-hid-overview.md) defines interface
roles; the [Control Plane Design](../control-plane-design.md) defines deferred UART control ownership.
Current client entry points are documented in the [Python host guide](../../../host/python/README.md),
[.NET host guide](../../../host/dotnet/README.md), and
[Host Tools Development](../../development/host-tools.md).

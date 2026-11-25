# UART/CDC Review Validation

The retained firmware correction resets core 1 before launch. Development evidence and remaining coverage are recorded
below; this record does not qualify a release.

## Scope and Identity

Development validation of the reviewed six-CDC/six-UART design on Pico/RP2040 serial `5303284748A07A1C`,
125 MHz, firmware `0.0.0`, Debug Probe `50543165611F6C9C`, fixed four-link fixture, RTS/CTS disabled.
No Pico 2 was attached.

Firmware source baseline is `da0b1e722b794cf7a0a81d14dcd9bbf5e47624b5`, including the reviewed mailbox,
lifecycle, readiness-query, and rejection-diagnostic fixes. The initial retained follow-ups repair HIL tooling, not the
firmware data path. The later [worker startup correction](#explicit-core-1-reset-follow-up) changes the core-launch
precondition. The refill experiments below were removed after comparison.

Each physical workload used a freshly flashed selected image. Upstream OpenOCD `0.12.0+dev-g46d9b60` identified
the BY25Q16ES flash normally and reported `Verified OK`; no capacity override was used. The distribution OpenOCD
could not identify `0x00154068`. Post-flash enumeration/readiness was checked before accepted workloads.

| Image label | ELF SHA-256 | Difference from baseline |
| --- | --- | --- |
| Baseline Pico | `6c9240acd54b7249d1351905ebe1fae3b8ddc97dd1decc06001fc6e493135ffe` | Normal production policies |
| HW refill candidate | `b3c573fade78d05ec6578474cf5c2f89120fbbeff780323dd8c7296133f13674` | Remove only HW TX launch's `UARTFR.BUSY` admission check |
| PIO refill candidate | `5a8699921dbbaac61a40b6fb2ed6e74b298168a22304c970ff105f800c6dd1f6` | Remove only PIO TX service's FIFO-empty admission gate |
| Profiled Pico | `5e294cbef7fa0001c501b481988123ce784fb6701bb1aa55a1bb1ac7b527a2e8` | Baseline with `PICO_UART_PROFILE_TIMING=1` |
| Pico 2 build | `43138b80e402161259f8fa7815c6684899211173e8b5512d722bdc09245a9f53` | RP2350 build; no physical testing |

## Decisions

| Review item | Decision |
| --- | --- |
| Mailbox/lifecycle/readiness simplification | Retain the reviewed baseline changes and native regressions. |
| Conservative TX admission | Retain both gates; isolated refill candidates did not justify removing them. |
| Configuration delays | Keep bounded synchronous settling; measured gaps do not justify another state machine. |
| Repeated line-coding comparisons | Keep explicit comparisons; a helper would add API/test surface without a demonstrated problem. |
| Combining status queries | Keep freshness at lifecycle boundaries; do not cache changing readiness to reduce lock counts. |
| Mailbox port identifier | Keep slot/payload validation and fault-injection coverage. |
| Similar HW/PIO sequences | Keep hardware-specific ordering; shared math and policies already have native tests. |
| Buffers, bridge budget, DMA threshold | Keep defaults; the measurements do not justify changes. |
| Aggregate bandwidth/resource limits | Preserve six CDC/six UART and all-or-nothing startup. |

## Evidence

### HIL Health Contract Repair

The old runner searched human monitor output for removed health-bit fields. Its first functional payload checks
passed, but final health parsing returned zero channel samples and correctly prevented acceptance/performance.
The repaired collector uses the existing host API's six non-consuming Channel and Diagnostic Feature reads.
It retains diagnostics and fails on lifecycle errors, missing/currentness failures, RX faults, and configuration
fault history. Stale captures receive bounded per-channel retries; transport/enumeration failures have bounded retries.
No stale or missing sample is converted into clean health.

The separate HIL project tests current, stale, and unavailable diagnostics; command failure; incomplete samples;
workflow failure propagation; and ceiling-boundary semantics. Historical counter/flag fixtures were migrated to the
current lifecycle/diagnostic contract.

### Matched Refill Experiments

All four fixture stages passed before each accepted comparison. Each rate used six concurrently verified streams,
the same host, 125 MHz clock, wiring, three-second block deadlines, and fresh flashes. Blocks were 32 bytes for
five-second small-payload runs or 1024 bytes for ten-second throughput runs. Baseline was repeated after candidates.
Times below are host-observed block round trips, not wire latency. Counts can include a final block finishing after
the nominal duration.

| Variant | Block bytes | Baud | Total verified bytes, six streams | Largest observed block, ms |
| --- | ---: | ---: | ---: | ---: |
| Baseline A | 32 | 115200 | 260576 | 5.330 |
| Baseline B | 32 | 115200 | 260000 | 5.318 |
| HW refill | 32 | 115200 | 244480 | 6.425 |
| PIO refill | 32 | 115200 | 258592 | 6.799 |
| Baseline A | 32 | 460800 | 525984 | 3.869 |
| Baseline B | 32 | 460800 | 514464 | 4.308 |
| HW refill | 32 | 460800 | 532640 | 4.134 |
| PIO refill | 32 | 460800 | 554592 | 4.263 |
| Baseline A | 1024 | 115200 | 687104 | 94.697 |
| Baseline B | 1024 | 115200 | 687104 | 93.729 |
| HW refill | 1024 | 115200 | 688128 | 94.260 |
| PIO refill | 1024 | 115200 | 688128 | 93.211 |
| Baseline A | 1024 | 460800 | 2620416 | 27.898 |
| Baseline B | 1024 | 460800 | 2617344 | 27.642 |
| HW refill | 1024 | 460800 | 2630656 | 29.579 |
| PIO refill | 1024 | 460800 | 2627584 | 28.569 |

All accepted rows had zero RX drops/errors and configuration faults. Large-block differences were below 1%.
PIO refill improved the tested 460800 small-block count by about 5-8%, but did not improve the 115200 case or
establish lower maximum latency. HW refill regressed the 115200 small-block workload. These short, single-board
candidate runs are insufficient to justify removing queue-budget gates, especially without a CTS fixture or RP2350
acceptance. Both candidates were removed. A future small-message-focused experiment should repeat PIO refill with
more samples and explicit latency criteria rather than assuming this possible gain generalizes.

Detailed accepted run records:

- Baseline [large A](archive/2026-10-23-170002Z-pico-125mhz-hil.md),
  [small A](archive/2026-10-23-171221Z-pico-125mhz-hil.md),
  [small B](archive/2026-10-23-172425Z-pico-125mhz-hil.md),
  [large B](archive/2026-10-23-172600Z-pico-125mhz-hil.md).
- HW refill [small](archive/2026-10-23-171456Z-pico-125mhz-hil.md) and
  [large](archive/2026-10-23-171632Z-pico-125mhz-hil.md).
- PIO refill [small](archive/2026-10-23-172029Z-pico-125mhz-hil.md) and
  [large](archive/2026-10-23-172156Z-pico-125mhz-hil.md).

### Configuration Isolation and Recovery

The profiled image first passed a separate [functional/concurrent run](archive/2026-10-23-172735Z-pico-125mhz-hil.md).
Timing arrays were then zeroed and read using non-halting SWD around the following workload:

- CDC1 loopback verified 267136 bytes in 2087 blocks of 128 bytes over 25 seconds; maximum host block was 13.373 ms.
- The other five channels received 200 settings requests across 115200, 230400, and 460800 baud. Linked endpoints
  were configured together and HID readiness was required. Ten CDC5 close/reopen cycles queued an old-session marker
  before close, then verified a distinct fresh marker after reopening.
- All six channels retained zero RX loss, RX errors, and configuration fault history.
- Maximum core passes were 542 us (core 0) and 676 us (core 1); maximum loop gaps were 545 and 678 us.
- Per-port maximum service gaps, CDC0-5, were 708, 673, 703, 684, 708, and 673 us.

These are observed, instrumented maxima, not worst-case bounds. This USB-fed loopback workload does not substitute
for an independently clocked external source. No measurement justifies an incremental DMA-settling state machine.

Separate fresh-image checks on the uninstrumented baseline:

| Check | Observed result |
| --- | --- |
| Bounded read stall | CDC1 accepted 2048 known bytes, held reads for 250 ms, then verified every byte at 115200; zero faults. |
| Intentional overflow | CDC1 accepted 16384 bytes and held reads for two seconds; exactly 6231 dropped bytes were reported. Drain of 10153 retained bytes plus dropped count accounts for the input. A fresh marker verified after explicitly draining old backlog. |
| Overflow isolation | Concurrent CDC5 loopback verified 86016 bytes over eight seconds without drops/errors. |
| Pending control/session loss | With all six channels open, CDC5 ran at feasible 300 baud, queued 1024 bytes, and received a 115200 request. HID reported `configuring` before a serial-identified USB bus reset. All six sessions returned closed, CDC5 reopened and verified a fresh marker, and no canceled-request timeout/fault appeared after 1.2 seconds. |
| Watchdog recovery | Halted core 1 through the selected probe, disabled watchdog debug-pause only for the injection, and allowed recovery. HID returned with watchdog reset cause, fresh uptime, normal clock, and verified CDC1 payload. Production pause-on-debug policy was not changed. |

An initial overflow-marker check failed because host `tcflush` does not discard queued device-side backlog. The
corrected procedure drains the retained stream before testing a new marker; overflow is not a session reset.
An initial 100-baud PIO setup was correctly rejected as clock-infeasible; 300 baud was used for the pending test.

An intrusive shortened-countdown DMA experiment did not produce valid acceptance evidence and left a host serial
operation blocked. It was stopped and the ordinary image was reflashed and verified. Do not count this as physical
DMA re-arm coverage. Native wrap/re-arm tests still pass; full-countdown endurance and a safe physical injection
procedure remain separate qualification work.

Early attempts also encountered post-flash readiness timeouts, rejected sub-minimum benchmark payload size, and
contended diagnostic captures. These are not included as passing comparisons. The tooling fixes do not establish
cold-power-on or immediate-post-SWD-reset reliability; accepted runs required successful readiness handshakes.

Local raw benchmark transcripts, timing samples, recovery assertions, and JSON captures remain in the testing
session cache. The tables above retain the measured results; local cache paths are not downloadable release evidence.

### Initial 60-Second Acceptance

The ordinary baseline was reflashed and verified after the intrusive tests. The
[final HIL record](2026-10-23-174031Z-pico-125mhz-hil.md) passed all four functional stages and all six concurrent
streams for 60 seconds per rate at 115200 and 460800 baud. Every per-rate measurement was current, with zero RX
drops, RX errors, and configuration fault history. The board was left running that normal image with CDC sessions
closed.

Both firmware targets build and pass compiled USB contracts. Native firmware tests pass normally and under ASan/UBSan;
firmware/host contract tests and firmware static analysis pass. Profiling and intentional-fault runs are diagnostic
evidence, not acceptance runs.

### Repository Recovery and Startup Follow-Up

The later review requested repeatable checks rather than another speculative data-path rewrite. The existing HIL
entry point now provides `recovery` and `startup`; their commands, acceptance, and limitations live in the
[recovery plan](../hil-recovery-and-stress.md#automated-loopback-recovery).
No firmware data-path, mailbox ownership, buffers, budgets, or TX gates changed in this follow-up.

#### Recovery Regression

The repository command's `--mode all` passed on the same identified RP2040, 125 MHz, baseline ELF/hash above,
starting at `2026-10-23T18:10:29Z`. The image was freshly flashed and verified, then allowed 15 seconds before
setup. This settling condition is recorded, not proposed as a fix for startup reliability.

| Repository check | Observed result |
| --- | --- |
| Configuration isolation | Twelve CDC5 changes across 115200/230400/460800, with verified markers; CDC1 simultaneously verified 52224 bytes over its five-second stream. |
| Session isolation | Five CDC5 close/reopen cycles queued distinct old payloads, then verified fresh markers on CDC5 and CDC1. |
| Bounded stall | CDC1 recovered all 2048 known bytes after reads were held while CDC5 verified 2688 bytes for at least 250 ms. |
| Intentional overflow | CDC1 retained 10160 bytes and reported exactly 6224 drops, accounting for all 16384 input bytes. Retained bytes passed the ordered-subsequence check. |
| Overflow recovery/isolation | CDC5 verified 21376 bytes during the two-second read stall; fresh markers passed on both loopbacks after draining device backlog. No further drops occurred. |
| Diagnostics/teardown | Current six-channel diagnostics, zero unexpected RX/configuration faults, and no endpoint cleanup error. Only intentional CDC1 drops were permitted. |

Unlike the earlier session-only harness, these checks are repository-runnable and retain partial results, exact image
hash, selected serial, and explicit failures in a new JSON file. Endpoint identities are checked before opening either
port. The overflow run is intentionally not a clean-health baseline for another test.

#### Startup Finding Before the Core-1 Reset Correction

Immediate-post-SWD CDC readiness failed again; it is not resolved by the tooling. Two bounded diagnostic attempts
observed CDC1 `starting`, with DTR open and uptime around 4.2 seconds. Core-1 service-load data was unavailable in
those samples. Follow-up debugger register capture failed; this does not establish a firmware root cause.
The normal image was reflashed after debugger work. No guessed flash override, host service change, firmware sleep,
or watchdog-policy change was used.

An initial first-snapshot observer at `18:04:34Z` saw two discovery/permission failures, then all channels closed
after 0.753 seconds. A subsequent CDC1 open still timed out. Consequently the delivered observer requires a
continuous closed-state window and uptime continuity, not just one successful snapshot.

The stability observation starting `18:08:59Z` retained 65 attempts, including 32 failures. Its first closed
observation arrived at 8.838 seconds, reporting uptime 910 ms and watchdog reset cause. It exhausted the 20-second
deadline before completing the required 12-second window; this is `FAIL`. The final shortened HID attempt hit the
overall deadline. Uptime in successful samples advanced normally. The discovery failures and watchdog reset do not
distinguish firmware, probe/reset sequence, or host causes.

The observer records each attempt and bounds potentially blocked HID operations in subprocesses. Tests cover
deadline exhaustion, post-readiness disconnect, reset/stalled uptime, rollover, invalid input, and preservation of
existing evidence.

#### Extended Acceptance and Software Checks

After the intentional overflow and diagnostic work, the ordinary baseline was freshly flashed and verified again.
The [extended acceptance record](2026-10-23-182232Z-pico-125mhz-hil.md) passed all four fixture stages and all six
streams for 300 seconds per rate at 115200 and 460800, using the exact baseline ELF/hash above. All per-rate and
final diagnostics were current, with zero RX drops/errors or configuration fault history. This extends the earlier
60-second observations; it remains a single-board, USB-paced workload.

The current HIL tooling suite passes its hardware-free checks, including the repository-run recovery and startup
commands. Both CLI help paths and affected documentation links were checked. No firmware source changed, so the
earlier build and sanitizer results apply to the unchanged baseline; they were not repeated as new firmware
qualification in this follow-up.

### Explicit Core-1 Reset Follow-Up

#### Cause and Retained Change

The driver launched core 1 without explicitly establishing the Pico SDK's reset-before-launch precondition.
`multicore_launch_core1()` requires core 1 to have been reset by system reset or `multicore_reset_core1()`; it does
not perform that reset itself. The retained firmware change calls `multicore_reset_core1()` immediately before launch,
after all backend checks and teardown succeed. It adds no firmware delay, retry, watchdog change, or transport policy.
Native regressions require reset before launch, no reset/launch on backend-check failure, and no reset/relaunch on
repeated initialization of a running worker.

This establishes the missing SDK precondition and resolves the observed post-SWD case in the trials below. It does
not prove a particular internal OpenOCD race or qualify every possible reset event. A failed baseline capture found
core 1 at a ROM address; a healthy capture found it in firmware. These snapshots alone do not establish the full
failure sequence.

An apparent core0-only programming improvement did not survive repetition: later trials failed startup or flash
verification. The loader remains unchanged and uses its normal SMP target. Subsequent SWD instability included
verification/reset failures and an all-`0xff` readback; those attempts are not accepted flashes. Normal verified
programming returned only after the user physically disconnected and reconnected both board and probe, confirming
their LEDs went off. No flash-capacity override or host service change was used.

#### Active Startup Comparison

The startup command now optionally verifies CDC1 readiness and a 154-byte 115200 8N1 loopback before accepting the
closed-state/uptime-stability window. A valid closed snapshot alone previously missed the worker failure. The probe
runs once; a failure cannot retry into a pass. See the
[recovery plan](../hil-recovery-and-stress.md#startup-observation)
for authorization, identity checks, deadlines, and teardown limitations.

Each trial below immediately followed a fresh, verified flash using the same normal loader, selected probe, and
100 kHz SWD speed. No 15-second settling delay was used. Times are from observer startup, not the physical reset edge.
Every passing row verified the payload and at least 12 seconds of closed-state observations with continuous uptime.
The failed rows reported closed channels but timed out awaiting active CDC readiness.

| Trial, in order | Image | Result | First valid snapshot uptime, ms | Observer completion, s |
| --- | --- | --- | ---: | ---: |
| Initial reproduction | Baseline | `FAIL` | 839 | 3.814 |
| Initial candidate | Explicit reset | `PASS` | 843 | 12.863 |
| Interleaved baseline A | Baseline | `FAIL` | 547 | 3.517 |
| Interleaved candidate A | Explicit reset | `PASS` | 857 | 12.867 |
| Interleaved baseline B | Baseline | `FAIL` | 546 | 3.481 |
| Interleaved candidate B | Explicit reset | `PASS` | 544 | 12.508 |
| Interleaved candidate C | Explicit reset | `PASS` | 859 | 12.883 |
| Final rebuilt image | Explicit reset | `PASS` | 827 | 13.037 |

Three baseline failures and five candidate passes support retaining the explicit reset, not a statistical reliability
guarantee. These are RP2040 post-SWD development observations, not cold-power-on or RP2350 qualification.

| Image label | ELF SHA-256 | Scope |
| --- | --- | --- |
| Explicit-reset candidate | `a8ae67b9cf7ca92049f69d22afd3a3f3be3e56e319c157870584c4b87edf0124` | First four candidate startup passes |
| Final Pico image | `e0f2fb2b41318894bb664c0cbc19a658a28dfecef7d39a66e8df5f78cc504657` | Final startup, recovery, and concurrent acceptance image |
| Pico 2 build | `4c9c9c944d35bab01c7bc097f8adcf3c4653bba451170f7d1358c27b21405353` | RP2350 build/USB checks only; no attached board |

Both candidate images have the same functional firmware delta from baseline: explicit core-1 reset. The final build
also includes the explanatory source comment and was rebuilt separately; hashes, not Git metadata alone, identify
the tested artifacts.

#### Final-Image Recovery and Host Validation

The final-image repository recovery command passed `--mode all` starting at `19:06:41Z`:

- Twelve CDC5 settings changes passed while CDC1 verified 53120 bytes over its five-second stream.
- Five CDC5 reopen cycles passed fresh-marker checks on both loopbacks.
- The bounded stall recovered all 2048 CDC1 bytes while CDC5 verified 2688 bytes during the 250 ms stall.
- Intentional overflow accounted for all 16384 input bytes: 10168 retained and 6216 reported drops.
  CDC5 verified 21248 bytes during the two-second stall; fresh markers passed after draining the backlog.
- Current diagnostics had no unexpected RX/configuration faults or cleanup error. A separate first invocation
  started before CDC re-enumeration and failed endpoint identity validation before opening a port; it remains a
  failed setup, not a passing workload. The accepted run freshly flashed again and verified enumeration first.

The firmware passes native tests normally and under ASan/UBSan. Both Pico and Pico 2 builds and their compiled
six-CDC/one-HID contracts pass, as does firmware static analysis. Targeted firmware, clock/version-policy, and
artifact-verifier Python checks also pass. The first contract-test invocation used the HIL-only environment and
failed collection for missing `tomlkit`; the accepted run used the existing host developer environment without
dependency changes.

#### Final-Image Concurrent Acceptance

After another fresh verified flash, the [final reset-corrected HIL record](2026-10-23-191806Z-pico-125mhz-hil.md)
passed all four functional stages and all six concurrent streams at 115200 and 460800 baud for 300 seconds per rate.
Every per-rate and final diagnostic was current, with zero RX drops/errors or configuration fault history. This run
used the final RP2040 ELF/hash above. Its Git fields truthfully name the parent commit and dirty worktree used during
testing; they do not imply that the parent commit alone contains the reset correction.

The board was left running this uninstrumented 125 MHz image. A final read-only HID check confirmed the identified
RP2040 and all six CDC sessions closed.

## Open Coverage

- RP2350 physical tests, independent UART ingress, and CTS fixture tests were not run.
- Full-countdown DMA wrap/re-arm endurance still needs a safe, validated injection procedure.
- Cold-power-on, watchdog, and USB-reset startup need event-specific qualification. The five successful trials above
  cover post-SWD startup only.

For release eligibility, see the [release procedure](../../deployment/releasing.md#release-hil-gates). Raw JSON and
debugger transcripts remain in the test-session cache; this record preserves the accepted measurements and failed
attempts.

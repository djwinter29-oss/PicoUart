# UART/CDC Review Validation

## Scope and Identity

Development validation of the reviewed six-CDC/six-UART design on Pico/RP2040 serial `5303284748A07A1C`,
125 MHz, firmware `0.0.0`, Debug Probe `50543165611F6C9C`, fixed four-link fixture, RTS/CTS disabled.
No Pico 2 was attached. This is not packaged-release qualification, a baud ceiling, or a losslessness guarantee.

Firmware source baseline is `da0b1e722b794cf7a0a81d14dcd9bbf5e47624b5`, including the reviewed mailbox,
lifecycle, readiness-query, and rejection-diagnostic fixes. Subsequent retained changes repair HIL tooling, not the
firmware data path. The experimental changes below were removed after comparison.

Each physical workload used a freshly flashed selected image. Upstream OpenOCD `0.12.0+dev-g46d9b60` identified
the BY25Q16ES flash normally and reported `Verified OK`; no capacity override was used. The distribution OpenOCD
could not identify `0x00154068`. Post-flash enumeration/readiness was checked before accepted workloads.

| Image under `build/` | ELF SHA-256 | Difference from baseline |
| --- | --- | --- |
| `review-baseline-pico/pico_uart.elf` | `6c9240acd54b7249d1351905ebe1fae3b8ddc97dd1decc06001fc6e493135ffe` | Normal production policies |
| `review-hw-refill-pico/pico_uart.elf` | `b3c573fade78d05ec6578474cf5c2f89120fbbeff780323dd8c7296133f13674` | Remove only HW TX launch's `UARTFR.BUSY` admission check |
| `review-pio-refill-pico/pico_uart.elf` | `5a8699921dbbaac61a40b6fb2ed6e74b298168a22304c970ff105f800c6dd1f6` | Remove only PIO TX service's FIFO-empty admission gate |
| `review-profile-pico/pico_uart.elf` | `5e294cbef7fa0001c501b481988123ce784fb6701bb1aa55a1bb1ac7b527a2e8` | Baseline with `PICO_UART_PROFILE_TIMING=1` |
| `review-final-pico2/pico_uart.elf` | `43138b80e402161259f8fa7815c6684899211173e8b5512d722bdc09245a9f53` | RP2350 build; no physical testing |

## HIL Health Contract Repair

The old runner searched human monitor output for removed health-bit fields. Its first functional payload checks
passed, but final health parsing returned zero channel samples and correctly prevented acceptance/performance.
The repaired collector uses the existing host API's six non-consuming Channel and Diagnostic Feature reads.
It retains diagnostics and fails on lifecycle errors, missing/currentness failures, RX faults, and configuration
fault history. Stale captures receive bounded per-channel retries; transport/enumeration failures have bounded retries.
No stale or missing sample is converted into clean health.

The separate HIL project passed 196 hardware-free tests, including current/stale/unavailable diagnostics, command
failure, incomplete samples, workflow failure propagation, and expected ceiling-boundary semantics. Historical
counter/flag fixtures were migrated to the current lifecycle/diagnostic contract.

## Matched Refill Experiments

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

- Baseline [large A](2026-10-23-170002Z-pico-125mhz-hil.md),
  [small A](2026-10-23-171221Z-pico-125mhz-hil.md),
  [small B](2026-10-23-172425Z-pico-125mhz-hil.md),
  [large B](2026-10-23-172600Z-pico-125mhz-hil.md).
- HW refill [small](2026-10-23-171456Z-pico-125mhz-hil.md) and
  [large](2026-10-23-171632Z-pico-125mhz-hil.md).
- PIO refill [small](2026-10-23-172029Z-pico-125mhz-hil.md) and
  [large](2026-10-23-172156Z-pico-125mhz-hil.md).

## Configuration Isolation and Recovery

The profiled image first passed a separate [functional/concurrent run](2026-10-23-172735Z-pico-125mhz-hil.md).
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

## Final Uninstrumented Acceptance

The ordinary baseline was reflashed and verified after the intrusive tests. The
[final HIL record](2026-10-23-174031Z-pico-125mhz-hil.md) passed all four functional stages and all six concurrent
streams for 60 seconds per rate at 115200 and 460800 baud. Every per-rate measurement was current, with zero RX
drops, RX errors, and configuration fault history. The board was left running that normal image with CDC sessions
closed. These are two short soak observations, not long-duration or full release-matrix qualification.

## Review Disposition and Remaining Gates

| Review item | Disposition |
| --- | --- |
| Mailbox/lifecycle/readiness simplification | Retain the reviewed baseline changes and their native regressions. |
| Conservative TX admission | Tested isolated candidates; retain both gates pending stronger evidence. |
| Configuration delays | Measured isolation and service gaps; retain bounded synchronous settling. |
| Repeated line-coding comparisons | Keep explicit comparisons; a new helper would add API/test surface without a demonstrated problem. |
| Combining status queries | Keep freshness at lifecycle boundaries; do not cache changing readiness merely to reduce lock counts. |
| Mailbox port identifier | Keep slot/payload validation and fault-injection coverage; removing it provides negligible benefit. |
| Similar HW/PIO sequences | Keep hardware-specific ordering; shared math/policies already have native tests. |
| Buffers, bridge budget, DMA threshold | Keep defaults; neither these measurements nor the prior tuning evidence establishes a reason to change them. |
| Aggregate bandwidth/resource limits | Preserve six CDC/six UART and all-or-nothing startup; supported baud is not aggregate USB throughput. |
| RP2350, CTS, long-duration/full-countdown qualification | Not physically covered; do not promote this record as complete release qualification. |

Both firmware targets build and pass compiled USB contracts. Native firmware targets pass 31/31 normally and under
ASan/UBSan; firmware/host contract tests and firmware static analysis pass. Profiling and intentional-fault runs
do not qualify the final image by themselves.

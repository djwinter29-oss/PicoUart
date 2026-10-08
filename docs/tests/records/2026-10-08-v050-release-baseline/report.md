# PicoUart v0.5.0 Release HIL Report — Already-Flashed Standard Pico

- **Date (UTC):** 2026-10-08
- **Scope:** Safe, documented HIL functional test (4 fixture stages) + a SHORT sustained performance baseline
  (115200 baud, 10 s, fixed fixture, no baud-ceiling sweep) against a board the user states is **already flashed**
  with the published `v0.5.0` artifact.
- **Backend:** GitHub Copilot CLI 1.0.93; raw evidence reviewed by Hermes.
- **Runner source:** `bd04be5dd27d88aa1a277ee48c4b51e593d207a6` (clean main during HIL).
- **Scope boundary:** No build, flash, OpenOCD, reset, source change, or dependency installation occurred during this
  Copilot HIL run. Earlier coordinator build/host checks in the same conversation exceeded the intended scope;
  they are excluded from these release-firmware results and their artifacts were not flashed.
- **Publication:** Copilot originally wrote this report and logs outside the repository. Hermes subsequently copies
  this reviewed evidence into a report-only branch (test/v0.5.0-release-report) for the user's PR; no firmware,
  locks, or existing results index changes.
- **Original report/log directory:** `/home/home/.cache/picouart-copilot-reports/release-v050/`

## 1. Binary Identity Verification (no flashing performed)

Per `docs/releasing.md`, release evidence must identify the exact artifact and its SHA-256. The downloaded release
bundle is at `/home/home/Downloads/PicoUart-v0.5.0/`.

```
sha256sum: 7ce1723b782cc1935c4c0a845e11fc366db2c98e914019d0aedd919f915fe134  pico_uart-v0.5.0-pico.elf
```

This matches the `pico_uart-v0.5.0-pico.elf` line in the bundled `SHA256SUMS-pico.txt` **exactly**. The artifact file
was only read (for hashing); it was **not reflashed** onto the board, consistent with the no-flash constraint.

**Binary-vs-runner identity caveat (limitation):** the hash above verifies the *downloaded artifact file on disk*. It
does **not** cryptographically prove that this exact binary is the one currently resident in the board's flash — USB
HID/CDC has no readback-hash mechanism in this firmware. The only on-device evidence available without
flashing/OpenOCD is the HID-reported semantic version string (`0.5.0`, captured below), which is consistent with, but
not cryptographic proof of, the exact flashed image. This is reported as a limitation, not bridged with a workaround.

## 2. Fixture Verification

`/dev/serial/by-id/` enumeration (read-only, via Python `os.listdir`, no `ls`/raw device write):

```
usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C-if00  (cdc0)
usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C-if02  (cdc1)
usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C-if04  (cdc2)
usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C-if06  (cdc3)
usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C-if08  (cdc4)
usb-PicoUart_PicoUart_CDC+HID_PIO_8N1_5303284748A07A1C-if0a  (cdc5)
usb-Raspberry_Pi_Debugprobe_on_Pico__CMSIS-DAP__50543165611F6C9C-if01  (Debug Probe, not used)
```

This matches the user-supplied interface mapping and the `docs/tests/hil-fixture-setup.md` fixed-fixture table:
CDC0 (UART0) ↔ CDC2 (UART2) crossed, CDC3 (UART3) ↔ CDC4 (UART4) crossed, CDC1 (UART1) and CDC5 (UART5) self-loopback.
The fixture was treated as already installed and fixed; **no jumpers were moved** during this session (wiring state
was not independently physically verifiable by this agent — see Limitations).

## 3. Interpreter Selection

- `host/python/.venv/bin/python`: imports `hid`; **does not** have `pyserial` installed (`ModuleNotFoundError: serial`).
- `tools/hil/.venv/bin/python`: imports **both** `hid` and `serial`, and has the editable `pico_uart` package on
  `sys.path` (resolves to `host/python/src/pico_uart`).

Because all commands used here need either HID-only or HID+serial access, `tools/hil/.venv/bin/python` was used
throughout (it is a strict superset). No packages were installed; no wrapper was missing — `tools/hil/runner/*.sh`
and `tools/hil/.venv/bin/pico-uart-hil` were present and used directly.

## 4. HID Health — Before Testing

Commands (raw logs: `pre-version.log`, `pre-overruns.log`, `pre-monitor.log`):

```
tools/hil/.venv/bin/python -m pico_uart version    -> rc=0, "0.5.0"
tools/hil/.venv/bin/python -m pico_uart overruns   -> rc=0, "cdc0=0 cdc1=0 cdc2=0 cdc3=0 cdc4=0 cdc5=0"
tools/hil/.venv/bin/python -m pico_uart monitor --duration 5
  -> rc=0; all 6 channels health=ready (cdc0/1 = 0x01, cdc2-5 = 0x21[ready,pio]); uart/cdc tx/rx all 0/0 (idle)
```

## 5. Functional HIL Test (all four fixed-fixture stages, no wiring changes)

Command executed (via `subprocess`, `cwd=/home/home/repo/PicoUart`; full raw stdout/stderr in `functional.log`):

```
tools/hil/runner/functional.sh \
  --pico-cdc0 /dev/serial/by-id/...-if00 --pico-cdc1 /dev/serial/by-id/...-if02 \
  --pico-cdc2 /dev/serial/by-id/...-if04 --pico-cdc3 /dev/serial/by-id/...-if06 \
  --pico-cdc4 /dev/serial/by-id/...-if08 --pico-cdc5 /dev/serial/by-id/...-if0a \
  --board pico --firmware-version 0.5.0 \
  --artifact /home/home/Downloads/PicoUart-v0.5.0/pico_uart-v0.5.0-pico.elf \
  --no-record --results-file <outside-repo path>
```

- **Start:** 2026-10-08T20:10:43.463Z  **End:** 2026-10-08T20:10:48.162Z  **Exit code: 0**
- `--no-record` suppressed the dated `docs/tests/records/` write (no repo file was created), per the no-repo-change
  constraint. No result JSON was written as a result (`--no-record` short-circuits that write unconditionally in
  `hil_test_suite/workflows/functional.py`); the full stage-by-stage PASS/FAIL transcript is preserved verbatim in
  `functional.log` instead.

| Stage | Connection | CDC endpoints | Result | Bytes verified |
|---|---|---|---|---|
| 1 | HW UART0 ↔ PIO UART2 | cdc0, cdc2 | **PASS** (both directions) | pico→peer 117 B, peer→pico 117 B |
| 2 | PIO UART3 ↔ PIO UART4 | cdc3, cdc4 | **PASS** (both directions) | pico→peer 117 B, peer→pico 117 B |
| 3 | HW UART1 loopback | cdc1 | **PASS** | 118 B |
| 4 | PIO UART5 loopback | cdc5 | **PASS** | 118 B |

HID health summary printed by the runner immediately before the stages: `health [cdc0=0x01, cdc1=0x01, cdc2=0x21,
cdc3=0x21, cdc4=0x21, cdc5=0x21]; overruns [all 0]; firmware=0.5.0` — unchanged after all 4 stages (overruns still
all 0, same health bits, firmware reported `0.5.0`).

## 6. Short Sustained Performance Baseline (115200 baud, 10 s, fixed fixture — NOT a ceiling sweep)

Command executed (full raw stdout/stderr in `performance-baseline.log`):

```
tools/hil/runner/performance.sh \
  --cdc0 ...-if00 --cdc1 ...-if02 --cdc2 ...-if04 --cdc3 ...-if06 --cdc4 ...-if08 --cdc5 ...-if0a \
  --rates 115200 --duration 10 \
  --board pico --firmware-version 0.5.0 \
  --artifact /home/home/Downloads/PicoUart-v0.5.0/pico_uart-v0.5.0-pico.elf \
  --no-record --results-file <outside-repo path>
```

- **Start:** 2026-10-08T20:11:08.709Z  **End:** 2026-10-08T20:11:29.242Z  **Exit code: 0**
- Single rate only (115200), single 10 s duration — this is the documented "short sustained baseline," explicitly
  **not** the `--incremental-performance` ceiling search and not the documented multi-rate sweep tables.

| Stream | Result | Bytes transferred | Throughput |
|---|---|---|---|
| cdc0 → cdc2 | **PASS** | 113,664 B | 11,308.3 B/s |
| cdc2 → cdc0 | **PASS** | 113,664 B | 11,308.3 B/s |
| cdc3 → cdc4 | **PASS** | 113,664 B | 11,308.3 B/s |
| cdc4 → cdc3 | **PASS** | 113,664 B | 11,308.3 B/s |
| cdc1 loopback | **PASS** | 113,664 B | 11,308.3 B/s |
| cdc5 loopback | **PASS** | 113,664 B | 11,308.3 B/s |

All six concurrent streams passed with verified returned payload bytes (not just traffic activity), consistent with
the documented pass criterion in `docs/tests/hil-fixture-test-plan.md`. Per-stream `TIME` lines (host thread-start,
first-send, first-nonempty-read UTC/monotonic) are preserved in `performance-baseline.log`; these are host-side
timestamps, not wire-level first-byte measurements, per the test-plan's own caveat.

## 7. HID Health — After Testing

Commands (raw logs: `post-version.log`, `post-overruns.log`, `post-status.log`, `post-monitor.log`):

```
tools/hil/.venv/bin/python -m pico_uart version    -> rc=0, "0.5.0"           (unchanged)
tools/hil/.venv/bin/python -m pico_uart overruns   -> rc=0, all 6 channels = 0 (unchanged, no new drops)
tools/hil/.venv/bin/python -m pico_uart status     -> rc=0, all 6 channels health=ready; uart/cdc tx/rx back to 0/0
tools/hil/.venv/bin/python -m pico_uart monitor --duration 5 -> rc=0, 51 report lines, no rx_error/control_error seen
```

`ring_peak` rose from 16 (idle boot value on cdc0/cdc3) to 1024 across all six channels during the performance phase —
this reflects the maximum ring-buffer depth observed during the sustained test (expected under load) and is not an
error counter; it is distinct from the `overruns` metric, which stayed at 0 throughout.

## 8. Overall Result

**PASS** — Functional HIL (4/4 stages) and the 115200 baud / 10 s sustained baseline (6/6 streams) both passed with
no overruns, no health-flag changes, no resets, and no disconnects observed on the board already flashed with the
`v0.5.0` artifact whose SHA-256 matches the published `SHA256SUMS-pico.txt`.

## 9. Limitations / Explicitly Not Done (per user's strict instructions)

- **No firmware/host build was run** — not CMake/ninja, not pytest firmware tests, nothing that invokes a compiler.
- **No flashing, no OpenOCD, no board reset** — the firmware under test is whatever was already resident on the
  board before this session; this report does not independently prove that image via flash readback.
- **No baud-ceiling sweep, no incremental performance search, no 30/60 s soak test** — only the documented short
  10 s / 115200 baud sustained baseline was selected by the coordinator; the user did not specify a rate or duration.
- **No wiring changes and no independent continuity check** — fixture state was assumed fixed and correct per the
  user's statement; this agent did not and could not physically verify jumper continuity.
- **No repository changes by the test worker** — no worker commits, pushes, PR, or edits to firmware/source/lock files. `--no-record`
  was used so no dated file was written under `docs/tests/records/`, and `build/hil-results.md` was not touched.
  The report and raw logs were copied unchanged from the worker's original directory to this report-only branch.
- **Binary-on-flash identity** is inferred from the HID-reported version string (`0.5.0`) plus the user's statement
  that this artifact was already flashed; it is not independently proven via a cryptographic on-device readback.

## 10. Artifacts in This Report Directory

```
report.md                     - this file
functional.log                - full command, timestamps, exit code, raw stdout/stderr for the 4-stage functional test
performance-baseline.log      - full command, timestamps, exit code, raw stdout/stderr for the 115200/10s baseline
pre-version.log / pre-overruns.log / pre-monitor.log   - HID health captured before testing
post-version.log / post-overruns.log / post-status.log / post-monitor.log - HID health captured after testing
```

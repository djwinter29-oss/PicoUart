# PicoUart

**Monitor six UARTs from your terminal, test runner, or browser.** The `pico-uart` package provides a Python API, CLI,
and local diagnostics dashboard for the PicoUart USB-to-UART bridge on Raspberry Pi Pico and Pico 2.

Inspect per-channel health, traffic statistics, and RX dropped-byte counts alongside your test results. Diagnostics use
a separate USB HID interface, so your existing serial tools can keep using the six USB CDC ports. This package does
not capture UART payloads or set serial baud rates and framing; use your serial tools for those tasks.

## Requirements

- Python 3.10 or newer
- A Raspberry Pi Pico or Pico 2 running compatible
	[PicoUart firmware](https://github.com/djwinter29-oss/PicoUart/releases), connected to this computer over USB
- Access to the board's HID interface; Linux may need
	[device permissions](https://github.com/djwinter29-oss/PicoUart/blob/main/tools/README.md#linux-hid-permissions)

## Install

Create a virtual environment with `python -m venv .venv`. Activate it with `. .venv/bin/activate` on Linux/macOS, or
`.venv\Scripts\Activate.ps1` in Windows PowerShell, then install:

```sh
python -m pip install --pre pico-uart
```

`--pre` allows development releases; omit it to select stable releases when available. For an isolated CLI/dashboard
installation instead, use `pipx install --pip-args="--pre" pico-uart` (requires pipx).

## Quick Start

Connect the flashed board, then read a status sample or start the dashboard:

```sh
pico-uart status
pico-uart web
```

Open <http://127.0.0.1:5000/>. The server binds to loopback and works with a normal browser; WebHID support and .NET
are not required. The dashboard shows channel health and traffic, RX dropped-byte counts, firmware and hardware
identity, clock, and temperature. Use `pico-uart web --port 5001` if the default port is occupied.

For browser-only diagnostics without installing Python, use the separate
[WebHID dashboard on GitHub Pages](https://djwinter29-oss.github.io/PicoUart/) in desktop Chrome or Edge, with the board
connected to the computer running that browser.

For multiple boards, put `--serial SERIAL` or `--device-path PATH` before the command:

```sh
pico-uart --serial ABC123 status
```

Use **Board** and **Rescan** in the dashboard to choose an attached board. See
[board selection and monitoring](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/development/host-tools.md#board-selection-and-shared-monitoring)
for selection and multi-user behavior.
Use one HID monitor/server per physical board; separate processes do not share its input reports or traffic totals.

## Test Automation

Have your test runner save a JSON status sample, run a timed monitor alongside the serial test, or record cumulative
RX dropped-byte counts before and after it. These commands can be used independently:

```sh
pico-uart status --json > uart-status.json
pico-uart monitor --duration 60 > uart-traffic.log
pico-uart overruns > uart-dropped-bytes.txt
```

Monitor output includes all six channels' health flags, UART/CDC byte deltas, and ring-buffer peaks. Byte deltas cover
the interval since the preceding successfully published status report and saturate at 65535. Dropped-byte counts from
`overruns` and buffer peaks are cumulative since boot, not per-test totals; account for resets and saturation.

`status` exits non-zero if its timeout expires without a valid report. `monitor` exits non-zero if it receives no valid
reports during its duration; it can succeed after one report even if later reads time out. Both exit non-zero on device
errors. Success does not prove continuous reporting or healthy channels: your runner must evaluate report availability,
flags, and values. Only `status` supports `--json`; `monitor` produces text. Missing reports or saturated values prevent
exact traffic accounting.

## Python API

```python
from pico_uart import PicoUartHid

with PicoUartHid() as board:
    status = board.read_status(timeout_ms=1000)
if status is None:
    raise RuntimeError("no PicoUart status report received")
print(status["channels"])
```

`read_status()` returns `None` on timeout; device access failures may raise `OSError` or `RuntimeError`. Use
`PicoUartHid(serial_number="ABC123")` to select a board. Other read methods include `read_board_status()`,
`read_overflow_counts()`, `read_hardware_info()`, and `read_configuration()`.

## Other Commands And Controls

Read-only commands include `hardware`, `config`, `temperature`, and `version`. Run `pico-uart --help` or
`pico-uart COMMAND --help` for options.

`toggle-led` changes the board LED. `reset` reboots the board and is refused unless firmware advertises reset support,
which is disabled by default. The API exposes equivalent `toggle_led()` and `reset_board()` methods.

The API's `save_configuration(profile, unsafe_overclock=False)` writes a supported boot-time clock profile;
`read_configuration_save()` reads the save result. A verified save automatically reboots the board, independently of
the optional reset command. Close all CDC ports and stop external UART traffic first; non-default profiles require
explicit unsafe-overclock acknowledgement. After reconnecting, inspect the active profile and fallback reason rather
than assuming the requested clock became active. A verified save does not establish overclock stability. See the
[HID report reference](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md)
for compatibility, save semantics, and overclock precautions before changing configuration.

## Troubleshooting

- **Board not found:** confirm compatible PicoUart firmware is flashed, USB is connected to the machine running Python,
	and the selected serial number or device path is correct. With no board attached, a HID command failing is expected.
- **Access denied:** check OS device permissions; see the Linux permission guide linked above. Do not run the dashboard
	as root to bypass permissions.
- **No valid reports:** check firmware compatibility and the USB connection. Do not interpret a timeout as zero traffic.

## Documentation

- [Project, firmware, and wiring](https://github.com/djwinter29-oss/PicoUart)
- [Host development and source-checkout setup](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/development/host-tools.md)
- [HID report reference and compatibility](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md)
- [Security and lab-only USB identity](https://github.com/djwinter29-oss/PicoUart/blob/main/SECURITY.md)

# PicoUart

PicoUart provides a Python client and command-line tool for monitoring and
controlling the HID diagnostics interface on the PicoUart six-channel USB-to-
UART bridge. UART data and line settings remain on the six USB CDC interfaces.

## Requirements

- Python 3.10 or newer
- A PicoUart-compatible RP2040 or RP2350 board
- Host access to its HID interface; Linux may require a udev rule or suitable
	permissions for the HID device node

## Install

```sh
python -m pip install pico-uart
```

## CLI

Read a status sample, watch live health reports, or launch the local dashboard:

```sh
pico-uart status
pico-uart status --json
pico-uart monitor --duration 10
pico-uart overruns
pico-uart web
```

The dashboard listens on `http://127.0.0.1:5000` and is bound to loopback. It
shows all six channels' health and traffic, RX overflow counts, firmware
version, and board temperature. It also offers an LED toggle and shows Reset
only when the firmware advertises reset support. Use `pico-uart web --port N`
to select another local port.

Other commands include `temperature`, `version`, `toggle-led`, and `reset`.
For a specific board, use the global `--serial SERIAL` or `--device-path PATH`
selector before the command, for example `pico-uart --serial ABC123 status`.

## Python API

```python
from pico_uart import PicoUartHid

with PicoUartHid() as board:
		status = board.read_status(timeout_ms=1000)
		if status is not None:
				print(status["channels"])
```

`read_status()` returns `None` when no report arrives before its timeout.
`read_board_status()` and `read_overflow_counts()` return decoded metadata.
The API's `toggle_led()` and `reset_board()` methods perform the same HID board
controls as the CLI.

## Important Notes

- Remote reset is disabled by default in firmware. The CLI and API refuse to
	reset unless firmware advertises that capability.
- The HID interface is for diagnostics and narrow board controls. It does not
	carry UART data or configure UART line coding.
- The published USB identity `cafe:4010` is a development/lab identity, not a
	commercial VID/PID.
- Linux users may need to grant access to the HID device node before running
	the tool.

## Documentation

- [CDC/HID behavior](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/usb/cdc-hid-overview.md)
- [HID report reference](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/usb/hid-report-reference.md)
- [UART pinout and wiring](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/uart-pinout.md)
- [Security and USB identity policy](https://github.com/djwinter29-oss/PicoUart/blob/main/SECURITY.md)
- [Host Python development guide](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/development/host-python.md)

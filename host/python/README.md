# pico-uart

Monitor a [PicoUart](https://github.com/djwinter29-oss/PicoUart) six-channel USB-to-UART adapter from your browser, the
command line, or Python. Works on Windows, Linux, and macOS with Python 3.10 or newer.

PicoUart appears on your computer as six ordinary serial ports. Keep using your usual terminal or serial library to
send and receive data. This package shows what the serial ports cannot: whether each port is running, how much
traffic it has carried, and whether any received bytes were dropped.

[Firmware releases](https://github.com/djwinter29-oss/PicoUart/releases) ·
[Report an issue](https://github.com/djwinter29-oss/PicoUart/issues)

UART pins use 3.3 V signals only. Never connect 5 V or RS-232 directly; see the
[hardware setup guide](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/hardware-setup.md) before wiring.

## Install

```sh
python -m pip install pico-uart
```

Install it into your project's virtual environment, or use `pipx install pico-uart` if you only need the command line
and dashboard. On Linux, follow the
[USB access setup](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/hardware-setup.md#linux-hid-permissions)
instead of running as root.

## Dashboard

Plug in a Pico or Pico 2 running PicoUart firmware, then run:

```sh
pico-uart dashboard
```

Open <http://127.0.0.1:5000/> on the same computer. Click **Rescan** after plugging in another board. Add
`--port 5001` if port 5000 is busy, and press `Ctrl+C` to stop.

Saving a clock profile here restarts the board. Close serial ports and stop traffic first; see the
[save rules](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md#save-and-reboot-safety).

## Command Line

| Command | Shows |
| --- | --- |
| `pico-uart status` | One port update; add `--json` for scripts |
| `pico-uart monitor --duration 60` | Live updates for 60 seconds |
| `pico-uart overruns` | Received bytes dropped on each port |
| `pico-uart version` | Firmware version |
| `pico-uart temperature` | Board temperature |

The CLI labels ports `cdc0` to `cdc5`; Python calls them channels 0 to 5. A `status` command reads one port update, so
other ports may still show as waiting and the first update has no traffic change to report. Use `monitor` for ongoing
updates. `pico-uart --help` lists every command, including board details and LED and reset controls. With several
boards plugged in, choose one by USB serial number: `pico-uart --serial ABC123 status`.

## Python

```python
from pico_uart import PicoUartHid

with PicoUartHid() as board:
    status = board.read_status(timeout_ms=1000)
    if status is None:
        print("No status update received")
    else:
        for channel in status["channels"]:
            if channel["sequence"] is not None:
                print(f'cdc{channel["id"]}: {channel["state"]}')
```

`read_status()` waits for one port update and returns the current snapshot of all six ports. Other ports may still have
state `"unknown"`. Possible states are `closed`, `starting`, `ready`, `configuring`, and `error`; `unknown` means no
report has arrived yet. Use `PicoUartHid(serial_number="ABC123")` to choose a board.

### Wait Before Sending

The board starts a port's UART when a program opens that serial port with DTR set, which serial libraries such as
pyserial do by default. In a script, wait until the board reports the port running at your baud rate before sending
the first bytes:

```python
import serial  # python -m pip install pyserial
from pico_uart import PicoUartHid

with PicoUartHid() as board, serial.Serial("COM5", 115200) as port:  # replace with your board's CDC0 port
    board.wait_channels_ready({0: 115200}, timeout_s=3.0)
    port.write(b"hello\n")
```

The host name `COM5` is only an example. Find your board's `CDC0` port in the
[serial-port guide](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/hardware-setup.md#find-the-serial-ports);
port 0 in the Python call is that same `CDC0` channel. The wait raises `TimeoutError` if it is not ready in time and
`OSError` if the board could not apply the settings.

### Other Methods

| Method | Returns |
| --- | --- |
| `read_board_status()` | Board identity, capabilities, and temperature |
| `read_overflow_counts()` | Dropped received bytes for each port |
| `read_channel_diagnostics()` | Detailed diagnostics for each port |
| `read_configuration()` | Pin assignments, buffer sizes, saved clock setting, and last reset cause |
| `toggle_led()` / `reset_board()` | Switch the board LED on or off to find it / restart the board |
| `save_configuration(profile, unsafe_overclock=False)` | Save boot clock settings; restarts the board |

Non-default profiles require `unsafe_overclock=True`.

## Troubleshooting

- **`PicoUart HID interface not found`:** check that the board runs PicoUart firmware, uses a data USB cable rather
  than a charge-only one, and is connected to the computer running Python.
- **Installation fails while building `hidapi`:** if pip cannot use a prebuilt package and the build fails, install
  the prerequisites listed by [hidapi](https://github.com/trezor/cython-hidapi).
- **`wait_channels_ready()` times out:** check that the intended serial port is open, DTR is set, and the baud rate
  matches the Python call. PIO-backed channels support 8N1 only.
- **No status updates:** a timeout means no update arrived, not that the port carried no traffic. Check package and
  firmware compatibility in the firmware releases linked above.

## More

- [Package changelog](https://github.com/djwinter29-oss/PicoUart/blob/main/host/python/CHANGELOG.md)
- [Developing from source](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/development/host-tools.md)
- [USB protocol reference](https://github.com/djwinter29-oss/PicoUart/blob/main/docs/design/usb/hid-report-reference.md)
- [Security policy](https://github.com/djwinter29-oss/PicoUart/blob/main/SECURITY.md)

Released under the [BSD 3-Clause License](https://github.com/djwinter29-oss/PicoUart/blob/main/LICENSE).

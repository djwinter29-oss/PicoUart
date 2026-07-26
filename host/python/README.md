# PicoUart HID Host Tool

`src/pico_uart_hid.py` accesses PicoUart's vendor-defined HID interface (`cafe:4010`)
for board controls and monitoring. It does not configure UARTs; CDC remains the
owner of UART line coding.

## Layout

| Path | Purpose |
| --- | --- |
| `src/` | Host tool sources |
| `tests/` | Pytest suite (no hardware required) |
| `requirements.txt` | Runtime dependency (`hidapi`) |
| `requirements-dev.txt` | Test extras (`pytest`) |

## Install

```sh
python3 -m pip install -r host/python/requirements.txt
```

For host automated tests (pytest):

```sh
python3 -m pip install -r host/python/requirements-dev.txt
python3 -m pytest
```

Firmware host C tests plus this pytest suite: [`firmware/tests/README.md`](../../firmware/tests/README.md).

Linux users may also need a udev rule that permits non-root access to vendor HID
devices. Run the tool with the privileges required by the local HID device node.

## Commands

```sh
python3 host/python/src/pico_uart_hid.py monitor --duration 10
python3 host/python/src/pico_uart_hid.py temperature
python3 host/python/src/pico_uart_hid.py version
python3 host/python/src/pico_uart_hid.py toggle-led
python3 host/python/src/pico_uart_hid.py reset
```

`version` prints the firmware semantic version (`MAJOR.MINOR.PATCH`) from HID
feature report 3. USB `bcdDevice` advertises major.minor only (for example
tag `v1.2.3` → HID `1.2.3`, `bcdDevice` `0x0102`).

`reset` sends HID arm (`3`) then reset (`2`) within the firmware arm window
(2 s). The board reboots through the watchdog and disconnects its USB
interfaces, then enumerates again after firmware startup completes.

The tool selects the HID collection with vendor usage page `0xFF00`, usage
`0x0001`; this avoids confusing the HID interface with any CDC ports.

# PicoUart HID Host Tool

`pico_uart_hid.py` accesses PicoUart's vendor-defined HID interface (`cafe:4010`)
for board controls and monitoring. It does not configure UARTs; CDC remains the
owner of UART line coding.

## Install

```sh
python3 -m pip install -r host/python/requirements.txt
```

Linux users may also need a udev rule that permits non-root access to vendor HID
devices. Run the tool with the privileges required by the local HID device node.

## Commands

```sh
python3 host/python/pico_uart_hid.py monitor --duration 10
python3 host/python/pico_uart_hid.py temperature
python3 host/python/pico_uart_hid.py toggle-led
python3 host/python/pico_uart_hid.py reset
```

`reset` immediately reboots PicoUart and disconnects its USB interfaces. The
board enumerates again after firmware startup completes.

The tool selects the HID collection with vendor usage page `0xFF00`, usage
`0x0001`; this avoids confusing the HID interface with any CDC ports.

# PicoUart HID Host Tool

`src/pico_uart_hid.py` accesses PicoUart's vendor-defined HID interface (`cafe:4010`)
for board controls and monitoring. It does not configure UARTs; CDC remains the
owner of UART line coding.

Status input reports use layout **v15** (63-byte payload after the Report ID).
Older hosts expecting v14 / 64-byte `PU` headers are incompatible — see
[`docs/hid-monitor.md`](../../docs/hid-monitor.md).

## Layout

| Path | Purpose |
| --- | --- |
| `src/` | Host tool sources |
| `tests/` | Pytest suite (no hardware required) |
| `requirements.txt` | Runtime dependency (`hidapi`) |
| `requirements-dev.txt` | Test extras (`pytest`) |
| `requirements-lock.txt` | Python 3.10+ release/CI dependency lock with artifact hashes |

## Install

Python 3.10 or newer is required.

```sh
python3 -m pip install -r host/python/requirements.txt
```

For host automated tests (pytest):

```sh
python3 -m pip install -r host/python/requirements-dev.txt
python3 -m pytest
```

CI and release qualification install `requirements-lock.txt` with
`pip --require-hashes`. Regenerate it from the repository root with:

```sh
uv pip compile host/python/requirements-dev.txt --universal --python-version 3.10 \
	--generate-hashes --no-emit-index-url --output-file host/python/requirements-lock.txt
```

Firmware host C tests plus this pytest suite: [`firmware/tests/README.md`](../../firmware/tests/README.md).

Linux users may also need a udev rule that permits non-root access to vendor HID
devices. Run the tool with the privileges required by the local HID device node.

## Commands

```sh
python3 host/python/src/pico_uart_hid.py monitor --duration 10
python3 host/python/src/pico_uart_hid.py temperature
python3 host/python/src/pico_uart_hid.py version
python3 host/python/src/pico_uart_hid.py overruns
python3 host/python/src/pico_uart_hid.py toggle-led
python3 host/python/src/pico_uart_hid.py reset
```

`monitor` exits nonzero if its duration expires without a valid status report.
Ordinary read timeouts, unexpected report IDs, and malformed status reports are
reported distinctly so missing telemetry is not mistaken for a successful run.

`version` prints the firmware semantic version (`MAJOR.MINOR.PATCH`) from HID
feature report 3. USB `bcdDevice` advertises major.minor only (for example
tag `v1.2.3` → HID `1.2.3`, `bcdDevice` `0x0102`).

`reset` reads HID board-status first and sends arm (`3`) then reset (`2`) only
when firmware advertises HID reset support (`reserved0` bit 0). Remote reset is
**disabled by default** (`PICO_UART_ALLOW_HID_RESET=0`); build with
`-DPICO_UART_ALLOW_HID_RESET=1` for the command to reboot the board. On the
default image the host tool exits nonzero instead of sending a no-op sequence.

The tool selects the unique HID collection with vendor usage page `0xFF00`,
usage `0x0001`; this avoids opening another collection that happens to share the
VID/PID. On hidapi backends that omit usage metadata, discovery only accepts a
unique collection with the exact PicoUart product and expected HID interface
number when that metadata is available. Ambiguous or contradictory discovery
results fail closed.

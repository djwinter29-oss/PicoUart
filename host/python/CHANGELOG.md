# Python package changelog

User-facing changes to the `pico-uart` Python package.

## Unreleased

### Changes

- Dashboard snapshots no longer include raw HID OS errors or device paths; details go to the log. The monitor
  reconnects with a capped exponential backoff (1 s to 8 s), and `BoardMonitor` and `BoardRegistry` are context
  managers.
- Input reads honor the absolute deadline and share transfer quarantine with Feature reports. Reaped dashboard readers
  remain owned until cleanup completes, and channel snapshots detect newer samples even when timestamps collide.
- `channel_ready` and the `BAUD_READY_TOLERANCE` constant are now public in `pico_uart.hid.client`.
- `read_board_status` and `read_hardware_info` now return the typed `BoardStatus` and `HardwareInfo` dictionaries.
- The package now ships a `py.typed` marker, so type checkers use its inline annotations.
- The module-level `read_*(device)` helpers now delegate to `PicoUartHid(device=device)`, so they share its bounded
  transfers and validation.
- The dashboard releases monitors for boards that stay unplugged for several scans.

### Breaking changes

- Python implementation modules moved under `pico_uart.hid` and `pico_uart.dashboard`; the CLI module is now
  `pico_uart.main`. The raw hidapi export is now `pico_uart.hid.transport.hid`, and `python -m pico_uart` is no
  longer supported. See the [module-path migration guide](../../docs/development/host-tools.md#python-module-path-migration)
  for replacements.
- Flask is now an optional dependency. Install `pico-uart[dashboard]` to use the dashboard; `pico-uart dashboard` is
  the preferred command, and `pico-uart web` remains as a compatibility alias.

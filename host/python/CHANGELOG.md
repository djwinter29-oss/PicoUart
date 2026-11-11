# Python package changelog

User-facing changes to the `pico-uart` Python package.

## Unreleased

### Breaking changes

- Python implementation modules moved under `pico_uart.hid` and `pico_uart.dashboard`; the CLI module is now
  `pico_uart.main`. The raw hidapi export is now `pico_uart.hid.transport.hid`, and `python -m pico_uart` is no
  longer supported. See the [module-path migration guide](../../docs/development/host-tools.md#python-module-path-migration)
  for replacements.
- Flask is now an optional dependency. Install `pico-uart[dashboard]` to use the dashboard; `pico-uart dashboard` is
  the preferred command, and `pico-uart web` remains as a compatibility alias.

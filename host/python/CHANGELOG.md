# Python package changelog

User-facing changes to the `pico-uart` Python package.

## Unreleased

### Changes

- Improved HID and dashboard reliability around reconnects, timed reads, cleanup, and uncertain save outcomes.
- Dashboard snapshots no longer expose raw HID OS errors or device paths; operational details stay in server logs.
  Non-ASCII CSRF tokens now fail safely with 403.
- Expanded and typed the client API with public channel-readiness support, typed status/hardware results, and inline
  typing.
- Flask is included in the base install, so `python -m pip install pico-uart` provides the API, CLI, and dashboard.
  `pico-uart dashboard` is preferred; `pico-uart web` remains an alias.

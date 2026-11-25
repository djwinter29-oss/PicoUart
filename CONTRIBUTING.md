# Contributing

Contributions to firmware, host tools, tests, and documentation are welcome. You do not need a board for many changes;
physical UART validation and release qualification require hardware.

## Before You Start

- Read [README.md](README.md) for project scope and the documentation index.
- Check [open issues](https://github.com/djwinter29-oss/PicoUart/issues) and
  [pull requests](https://github.com/djwinter29-oss/PicoUart/pulls) to avoid duplicate work.
- For new features, design changes, or substantial changes, open an issue to agree on scope before starting.
  Well-scoped bug fixes do not require prior approval.
- When reporting a bug, include reproduction steps, expected and actual behavior, and relevant versions and logs.
  For hardware issues, also include the board target, firmware artifact, and wiring. Remove sensitive information.

## Development Setup

- Firmware builds: [firmware/build-and-config.md](firmware/build-and-config.md) and
  [tools/README.md](tools/README.md).
- Host tools (Python/dotnet/Direct): [docs/development/host-tools.md](docs/development/host-tools.md).
- Cursor Cloud snapshot setup: [docs/development/cursor-cloud.md](docs/development/cursor-cloud.md).
- Repository guidance, including agent-assisted work: [AGENTS.md](AGENTS.md).

Use Linux or WSL2 Ubuntu for firmware development and HIL wrappers. Follow the linked guides for host-platform support
and dependencies rather than installing every tool for every contribution.

Before using an attached board or connected equipment, follow the
[hardware authorization rules](docs/tests/README.md#hardware-authorization).

## Validate Your Change

Run the smallest relevant checks first, then broaden validation for shared behavior or cross-project contracts.
Use [Repository Tools](tools/README.md) for commands:

| Change | Run |
| --- | --- |
| Firmware | Run the [hardware-free checks](docs/tests/README.md#without-hardware). |
| Python/dotnet | Run relevant tests and lint/type checks; see [host development](docs/development/host-tools.md). |
| Dashboards | Run the [dashboard checks](docs/development/dashboard-development.md). |
| Documentation | Check links and examples, then run `git diff --check`. |

For Python checks, use the virtual environment from the
[host setup guide](docs/development/host-tools.md#python-environment-setup).

Hardware HIL is not required for every pull request. If hardware is unavailable, report that limitation rather than
claiming hardware validation. Exact-artifact HIL is required for release qualification; see the
[release gates](docs/deployment/releasing.md#release-hil-gates).

## Engineering Conventions

- Keep changes small and focused. Preserve the six CDC-to-UART design unless a change explicitly requires otherwise.
- Use Pico SDK and TinyUSB conventions for firmware, keep pin mapping separate from transport logic, and follow the
  Python lint and type-check settings in [pyproject.toml](host/python/pyproject.toml).
- Keep source and Markdown prose within 120 columns. Keep documentation focused, with one authoritative home for each
  contract, procedure, or result; link to it instead of copying details.
- Use Doxygen for exported C enums, structs, typedefs, macros, and functions. In source files, document important
  internal state and non-trivial helpers. Describe meaningful struct members and enum values; prefer `@brief`,
  `@param`, `@return`, and `@copydoc`.
- Mark intentional simplifications with `ponytail:`. If a shortcut has a known ceiling, document why it is acceptable
  and what would replace it if the ceiling becomes a problem.
- Add a focused regression check for non-trivial fixes or new behavior.
- Update matching docs when behavior changes; do not duplicate content across docs.

## Pull Requests

- Explain the problem, the change, and any compatibility impact; link the related issue when applicable.
- List checks run and their results, and identify skipped checks and why they were skipped.

## Reporting Security Issues

Do not publish vulnerability details; follow the reporting instructions in [SECURITY.md](SECURITY.md).

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

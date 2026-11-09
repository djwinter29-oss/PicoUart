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
- Host tools (Python/.NET/WebHID): [docs/development/host-tools.md](docs/development/host-tools.md).
- Agent-assisted work: [AGENTS.md](AGENTS.md) for repository guidance and hardware safety boundaries.

Use Linux or WSL2 Ubuntu for firmware development and HIL wrappers. Follow the linked guides for host-platform support
and dependencies rather than installing every tool for every contribution.

An attached board is not permission to modify it or exercise connected devices. Before flashing, resetting, changing
board or UART configuration, sending board-control commands, or transmitting UART data, obtain explicit authorization
from the equipment owner or responsible operator. Identify the board and connected devices, stay within the authorized
scope, and do not perform these actions merely to validate documentation or UI changes.
You may authorize work on equipment you own; shared or third-party boards and connected devices require permission
from their owner or responsible operator.

## Validate Your Change

Run the smallest relevant checks first, then broaden validation for shared behavior or cross-project contracts.
Use [Repository Tools](tools/README.md) for commands:

| Change | Validation |
| --- | --- |
| Firmware | Relevant native tests and contracts; build both `pico` and `pico2`. See [test scope](docs/development/firmware-testing.md#test-scope). |
| Python, .NET, or browser tools | Tests for the affected host application and any shared assets or contracts; see [host development](docs/development/host-tools.md). |
| Documentation only | Check links, examples, and rendered formatting; run `git diff --check`. |

For Python tests, use the virtual environment from the
[host setup guide](docs/development/host-tools.md#python-environment-setup). When using the host-test wrapper, set
`PYTHON_EXE=host/python/.venv/bin/python` so it does not try to install into the system interpreter.

Hardware HIL is not required for every pull request. If hardware is unavailable, report that limitation rather than
claiming hardware validation. Exact-artifact HIL is required for release qualification; see the
[release gates](docs/deployment/releasing.md#release-hil-gates).

## Pull Requests

- Keep changes small and focused; follow the [shared coding conventions](.github/copilot-instructions.md), including
  C, Python, Markdown, and Doxygen requirements.
- Preserve the 6 CDC to 6 UART design unless the change is explicitly about that design.
- Keep board pin mapping separate from USB and UART transport logic.
- Add a focused regression check for non-trivial fixes or new behavior.
- Update matching docs when behavior changes; do not duplicate content across docs.
- Explain the problem, the change, and any compatibility impact; link the related issue when applicable.
- List checks run and their results, and identify skipped checks and why they were skipped.

## Reporting Security Issues

Do not open a public issue for suspected vulnerabilities. Follow [SECURITY.md](SECURITY.md) instead.

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

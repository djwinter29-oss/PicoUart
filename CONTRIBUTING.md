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
- Cursor Cloud snapshot setup: [docs/development/cursor-cloud.md](docs/development/cursor-cloud.md).
- Repository guidance, including agent-assisted work: [AGENTS.md](AGENTS.md).

Use Linux or WSL2 Ubuntu for firmware development and HIL wrappers. Follow the linked guides for host-platform support
and dependencies rather than installing every tool for every contribution.

Before using an attached board or its connected equipment, get the owner's explicit permission to flash or reset it,
change its configuration, use board controls, or send UART traffic.

## Validate Your Change

Run the smallest relevant checks first, then broaden validation for shared behavior or cross-project contracts.
Use [Repository Tools](tools/README.md) for commands:

| Change | Run |
| --- | --- |
| Firmware | Run the [hardware-free checks](docs/tests/README.md#without-hardware). |
| Python/.NET | Run relevant tests and lint/type checks; see [host development](docs/development/host-tools.md). |
| Dashboards | Run the [dashboard checks](docs/development/dashboard-development.md). |
| Documentation | Check links and examples, then run `git diff --check`. |

For Python checks, use the virtual environment from the
[host setup guide](docs/development/host-tools.md#python-environment-setup).

Hardware HIL is not required for every pull request. If hardware is unavailable, report that limitation rather than
claiming hardware validation. Exact-artifact HIL is required for release qualification; see the
[release gates](docs/deployment/releasing.md#release-hil-gates).

## Pull Requests

- Keep changes small and focused; follow the [shared coding conventions](.github/copilot-instructions.md), including
  C, Python, Markdown, and Doxygen requirements.
- Add a focused regression check for non-trivial fixes or new behavior.
- Update matching docs when behavior changes; do not duplicate content across docs.
- Explain the problem, the change, and any compatibility impact; link the related issue when applicable.
- List checks run and their results, and identify skipped checks and why they were skipped.

## Reporting Security Issues

Do not publish vulnerability details; follow the reporting instructions in [SECURITY.md](SECURITY.md).

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

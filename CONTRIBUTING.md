# Contributing

Thanks for considering a contribution to PicoUart. This is an embedded firmware project (RP2040/RP2350) with Python,
.NET, and browser-based host tools; most end-to-end validation needs physical hardware.

## Before You Start

- Read [README.md](README.md) for project scope, architecture, and documentation index.
- Read [AGENTS.md](AGENTS.md) for environment setup and what can be verified without hardware.
- Check open issues and pull requests to avoid duplicate work.
- For anything beyond a small fix, open an issue first to discuss the approach.

## Development Setup

- Firmware builds: [firmware/build-and-config.md](firmware/build-and-config.md) and
  [tools/README.md](tools/README.md).
- Host tools (Python/.NET/WebHID): [docs/development/host-tools.md](docs/development/host-tools.md).
- Repository test layout: [docs/development/firmware-testing.md](docs/development/firmware-testing.md).

Run the relevant test suite(s) for the area you changed before opening a pull request:

```sh
tools/host/setup-venv.sh
tools/validation/run-host-tests.sh
tools/validation/validate.sh --skip-build
```

## Pull Requests

- Keep changes small and focused; match the existing C, Python, and Markdown style.
- Preserve the 6 CDC to 6 UART design unless the change is explicitly about that design.
- Keep board pin mapping separate from USB and UART transport logic.
- Update matching docs when behavior changes; do not duplicate content across docs.
- Note which checks you ran (host tests, firmware build, HIL) and which you could not run.
- Physical hardware-in-the-loop (HIL) results are required for release qualification but not for every pull request;
  see [docs/releasing.md](docs/releasing.md#release-hil-gates) for what gates a release.

## Reporting Security Issues

Do not open a public issue for suspected vulnerabilities. Follow [SECURITY.md](SECURITY.md) instead.

## Code of Conduct

This project follows the [Code of Conduct](CODE_OF_CONDUCT.md).

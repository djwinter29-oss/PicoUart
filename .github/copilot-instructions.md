# PicoUart Copilot Instructions

- Keep changes small, preserve the six CDC-to-UART design, and follow the repository's existing patterns.
- Use Pico SDK and TinyUSB conventions for firmware. Keep board pin mapping separate from USB and UART transport logic.
- Read the relevant design, test, or release guide before changing behavior. Do not copy their procedures here.
- Follow [CONTRIBUTING.md](../CONTRIBUTING.md) for shared conventions.
- Before adding code, check whether it is needed, already exists, or can use the standard library or an installed
  dependency. Prefer the smallest implementation and avoid unrequested abstractions or dependencies.
- Fix root causes rather than symptoms. Validate inputs at trust boundaries and handle errors that could lose data.
- For non-trivial behavior, add a focused regression check. Preserve type safety and run the smallest relevant check.
- Mark intentional simplifications with `ponytail:` and document a known ceiling, why it is acceptable, and the upgrade
  path.
- Public C API and important internal state use concise Doxygen comments; document meaningful fields and enum values.
- Follow the [QSPI override procedure](../firmware/build-and-config.md#qspi-auto-detection-override) only after
  confirming flash capacity from the chip marking and datasheet. Never guess capacity or use the override to hide
  failed SWD access.
- Use the HIL test plan's [speed-sweep rules](../docs/tests/hil-fixture-test-plan.md#baseline-and-speed-ceiling)
  when recording a ceiling search.

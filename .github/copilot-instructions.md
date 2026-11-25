# PicoUart Copilot Instructions

- Follow the [engineering conventions](../CONTRIBUTING.md#engineering-conventions).
- Read the relevant design, test, or release guide before changing behavior, and follow existing repository patterns.
- Before adding code, check whether it is needed, already exists, or can use the standard library or an installed
  dependency. Prefer the smallest implementation and avoid unrequested abstractions or dependencies.
- Fix root causes rather than symptoms. Validate inputs at trust boundaries and handle errors that could lose data.
- Preserve type safety and run the smallest relevant check.
- Follow the [QSPI override procedure](../firmware/build-and-config.md#qspi-auto-detection-override) only after
  confirming flash capacity from the chip marking and datasheet. Never guess capacity or use the override to hide
  failed SWD access.
- Use the HIL test plan's [speed-sweep rules](../docs/tests/hil-fixture-test-plan.md#baseline-and-speed-ceiling)
  when recording a ceiling search.

---
name: pico-uart-board-testing
description:
  "Use when: testing, bring-up, flashing, debugging, or validating a PicoUart board, including the four-stage UART
  fixture, concurrent serial stress benchmarking, and bidirectional UART traffic."
argument-hint: "Describe the board connection or test failure"
user-invocable: true
disable-model-invocation: false
---

# PicoUart Board Testing

Use the repository's [test overview](../../../docs/tests/README.md) to choose a test. Read the relevant canonical guide
before acting:

- [Fixture wiring](../../../docs/tests/hil-fixture-setup.md)
- [HIL procedures and speed-sweep rules](../../../docs/tests/hil-fixture-test-plan.md)
- [Release gates](../../../docs/deployment/releasing.md)
- [Record format and clock handling](../../../docs/tests/records/README.md)
- [Firmware flashing and recovery](../../../firmware/build-and-config.md)

Follow the [hardware authorization rules](../../../docs/tests/README.md#hardware-authorization).
Keep results with the exact artifact and follow the record guide.

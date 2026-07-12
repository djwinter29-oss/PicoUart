# Firmware

This folder contains the Pico SDK firmware project `pico_uart` for PicoUart.

The current USB implementation enumerates 6 CDC ports and leaves each port in local echo mode.

## Build

1. Install CMake, Ninja, an ARM GCC toolchain, and the Pico SDK.
2. Set `PICO_SDK_PATH` to your `pico-sdk` checkout.
3. Configure and build:

```powershell
cmake -S firmware -B firmware/build -G Ninja
cmake --build firmware/build
```

## Notes

- Default board is `pico`.
- Override the board with `-DPICO_BOARD=<board>` when needed.
- The current firmware is a USB CDC echo scaffold and does not yet bridge traffic to UARTs.
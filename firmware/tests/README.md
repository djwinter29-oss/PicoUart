# Host-side automated tests (no Pico board required)

## Layout

| Path | Purpose |
| --- | --- |
| `firmware/tests/` | Native C unit tests (Unity + CMake/CTest) for firmware logic |
| `firmware/tests/test_ring_buffer.c` | Ring-buffer occupancy, wrap, overwrite recovery |
| `firmware/tests/test_line_coding.c` | USB CDC line-coding parse table, baud bounds, PIO 8N1 gate |
| `firmware/tests/test_dma_progress.c` | RX DMA progress wrap + RP2040/RP2350 COUNT mask math |
| `firmware/tests/stubs/` | Host stubs for Pico SDK headers (for example `hardware/sync.h`) |
| `firmware/tests/third_party/unity/` | Vendored [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.0 |
| `host/python/src/` | HID host tool package/scripts |
| `host/python/tests/` | HID parsers/contracts, HID descriptor report-count sync, serial tool arg validation |

Mailbox, TinyUSB CDC callbacks, and on-target DMA IRQ re-arm are exercised via
the board-testing skill / `docs/releasing.md` HIL gate. Pure RX DMA progress
arithmetic is covered by `test_dma_progress`.

## Run everything

```sh
tools/linux/test-host.sh
```

Or via the combined script (also builds firmware unless `--skip-build`):

```sh
tools/linux/test.sh --skip-build
```

## C tests only

```sh
cmake -S firmware/tests -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

## Python tests only

```sh
python3 -m pip install -r host/python/requirements-dev.txt
python3 -m pytest
```

# Host-side automated tests (no Pico board required)

## Layout

| Path | Purpose |
| --- | --- |
| `firmware/tests/` | Native C unit tests (Unity + CMake/CTest) for firmware logic |
| `firmware/tests/test_ring_buffer.c` | Ring-buffer occupancy, wrap, overwrite recovery |
| `firmware/tests/test_line_coding.c` | USB CDC line-coding parse table, baud bounds, PIO 8N1 gate |
| `firmware/tests/test_dma_progress.c` | RX DMA progress wrap, COUNT mask math, pause-settle sample policy |
| `firmware/tests/test_txstall_wait.c` | PIO TXSTALL re-assert wait microseconds vs baud |
| `firmware/tests/test_cdc_soft_pending.c` | Soft-pending deadline coalesce, reset-cancellation-suppresses-timeout regression, reject generation bump policy, CONTROL_PENDING ownership |
| `firmware/tests/test_topology.c` | Logical port, GPIO, UART, and PIO state-machine assignment validation |
| `firmware/tests/test_backend_policy.c` | Backend idle, DMA, PIO TX, IRQ-owner, and worker-heartbeat policy |
| `firmware/tests/test_led_policy.c` | Manual/USB-activity board LED merge and activity-window timeout/extension |
| `firmware/tests/test_dma_claim.c` | HW UART RX/TX DMA channel claim/rollback and NULL-input fast-fail fault injection |
| `firmware/tests/test_resource_claim.c` | PIO UART SM + DMA channel claim/rollback and NULL-input fast-fail fault injection |
| `firmware/tests/stubs/` | Host stubs for Pico SDK headers (for example `hardware/sync.h`) |
| `firmware/tests/third_party/unity/` | Vendored [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.0 |
| `host/python/src/` | HID host tool package/scripts |
| `host/python/tests/` | HID parsers/contracts, HID descriptor report-count sync, serial tool arg validation |

Mailbox, TinyUSB CDC callbacks, and on-target DMA IRQ re-arm are exercised via
the board-testing skill / `docs/releasing.md` HIL gate. Host Unity tests cover
pure policy helpers used by those paths (RX DMA progress, pause-settle samples,
TXSTALL wait, backend idle/re-arm/TX action/IRQ owner/heartbeat, CDC soft-pending,
topology) plus the seam-based HW/PIO resource-claim helpers and their NULL-input
fast-fail branches.

## Run everything

```sh
tools/test-host.sh
```

The native C tests run before Python dependency checks. Use `--skip-python` in
minimal environments that do not have pip or the host-test virtual environment.
CI also runs `tools/test-host.sh --sanitize` (ASan/UBSan) for the Unity
targets.

```sh
tools/test-host.sh --skip-python
```

Use `--sanitize` to rebuild the Unity tests with ASan/UBSan (CI does this on
Linux).

Or via the combined script (also builds firmware unless `--skip-build`):

```sh
tools/test.sh --skip-build
```

## C tests only

```sh
cmake -S firmware/tests -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

## Python tests only

CI uses Python 3.12. On Debian/Ubuntu, install `python3-venv` before creating
the repository virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install --require-hashes -r host/python/requirements-lock.txt
.venv/bin/python -m pytest
```

Use `tools/test-host.sh` after setup to run the native C and Python suites
together.

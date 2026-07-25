# Host-side automated tests (no Pico board required)

## Layout

| Path | Purpose |
| --- | --- |
| `tests/c/` | Native C unit tests (Unity + CMake/CTest) for firmware logic |
| `tests/c/stubs/` | Host stubs for Pico SDK headers (for example `hardware/sync.h`) |
| `tests/c/third_party/unity/` | Vendored [Unity](https://github.com/ThrowTheSwitch/Unity) v2.6.0 |
| `tests/python/` | Pytest suite for `host/python` HID parsers |

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
cmake -S tests/c -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

## Python tests only

```sh
python3 -m pip install -r host/python/requirements-dev.txt
python3 -m pytest
```

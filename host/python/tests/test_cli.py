"""Hardware-free tests for the public PicoUart command-line interface."""

from __future__ import annotations

import runpy
import re

import pytest

from pico_uart import cli


@pytest.mark.parametrize(
    ("command", "expected_output", "expected_action"),
    [
        ("temperature", "temperature=23.50 C\n", "temperature"),
        ("version", "1.2.3\n", "version"),
        ("overruns", "cdc0=0 cdc1=1 cdc2=2 cdc3=3 cdc4=4 cdc5=5\n", "overruns"),
        ("hardware", '{"mcu": "RP2040", "mcu_id": 1, "system_clock_hz": 250000000}\n', "hardware"),
        ("config", '{"reset_cause": 2, "stored_profile": 0}\n', "config"),
        ("toggle-led", "", "toggle-led"),
        ("reset", "", "reset"),
    ],
)
def test_main_dispatches_board_commands(monkeypatch, capsys, command, expected_output, expected_action):
    calls = []

    class FakeClient:
        def __init__(self, serial_number, device_path):
            calls.append(("selectors", serial_number, device_path))

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            pass

        def read_board_temperature(self):
            calls.append("temperature")
            return 23.5

        def read_firmware_version(self):
            calls.append("version")
            return "1.2.3"

        def read_overflow_counts(self):
            calls.append("overruns")
            return list(range(6))

        def read_hardware_info(self):
            calls.append("hardware")
            return {"mcu": "RP2040", "mcu_id": 1, "system_clock_hz": 250_000_000}

        def read_configuration(self):
            calls.append("config")
            return {"stored_profile": 0, "reset_cause": 2}

        def toggle_led(self):
            calls.append("toggle-led")

        def reset_board(self):
            calls.append("reset")

    monkeypatch.setattr(cli, "PicoUartHid", FakeClient)

    assert cli.main(["--serial", "board-1", command]) == 0
    assert capsys.readouterr().out == expected_output
    assert calls == [("selectors", "board-1", None), expected_action]


def test_main_status_text_retries_empty_reads_and_selects_device_path(monkeypatch, capsys):
    status = {
        "channels": [
            {
                "id": index, "sequence": 17 if index == 0 else None,
                "state": "ready", "uart_measurement_state": 1,
                "traffic": {
                    "uart_tx": 1, "uart_rx": 2, "usb_tx": 3, "usb_rx": 4,
                },
                "ring_high_watermark": 32,
            }
            for index in range(6)
        ],
    }
    read_timeouts = []

    class FakeClient:
        def __init__(self, serial_number, device_path):
            assert serial_number is None
            assert device_path == "/dev/hidraw7"

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            pass

        def read_status(self, timeout_ms):
            read_timeouts.append(timeout_ms)
            return None if len(read_timeouts) == 1 else status

    monkeypatch.setattr(cli, "PicoUartHid", FakeClient)

    assert cli.main(["--device-path", "/dev/hidraw7", "status", "--timeout", "1"]) == 0
    assert len(read_timeouts) == 2
    output = capsys.readouterr().out
    assert output.startswith("cdc0 state=ready measurement=1")
    assert "seq=17" in output
    assert "cdc1 waiting-for-input" in output


def test_monitor_preserves_all_six_hil_health_tokens_in_one_publication_round(monkeypatch, capsys):
    from helpers import FakeHidDevice, channel_bytes
    from pico_uart.client import PicoUartHid

    device = FakeHidDevice({}, inputs=[
        (10 + index, channel_bytes(sequence=1)) for index in range(6)
    ])
    client = PicoUartHid(device=device)
    samples = [client.read_status(0) for _ in range(6)]

    class Reader:
        def read_status(self, timeout_ms):
            return samples.pop(0)

    now = iter((0.0, 0.5, 0.51, 0.52, 0.53, 0.54, 0.55, 1.0))
    monkeypatch.setattr(cli.time, "monotonic", lambda: next(now))
    cli.monitor(Reader(), 1.0)
    assert set(re.findall(r"cdc([0-5]) state=(\w+)", capsys.readouterr().out)) == {
        (str(index), "ready") for index in range(6)
    }


def test_main_monitor_dispatches_configured_duration(monkeypatch):
    calls = []

    class FakeClient:
        def __init__(self, *_selectors):
            pass

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            pass

    monkeypatch.setattr(cli, "PicoUartHid", FakeClient)
    monkeypatch.setattr(cli, "monitor", lambda client, duration: calls.append(duration))

    assert cli.main(["monitor", "--duration", "1.5"]) == 0
    assert calls == [1.5]


@pytest.mark.parametrize(
    ("arguments", "message"),
    [
        (["monitor", "--duration", "nan"], "--duration must be a finite"),
        (["monitor", "--duration", "0"], "--duration must be a finite"),
        (["status", "--timeout", "inf"], "--timeout must be a finite"),
        (["web", "--port", "65536"], "--port must be between"),
    ],
)
def test_main_rejects_invalid_command_arguments(arguments, message):
    with pytest.raises(SystemExit, match=message):
        cli.main(arguments)


def test_main_web_starts_server_with_port_and_selector(monkeypatch):
    from pico_uart.web import app as web_app

    calls = []
    monkeypatch.setattr(
        web_app,
        "run_server",
        lambda *arguments: calls.append(arguments),
    )

    assert cli.main(["--device-path", "/dev/hidraw7", "web", "--port", "5500"]) == 0
    assert calls == [(5500, None, "/dev/hidraw7")]


@pytest.mark.parametrize("error", [OSError("bind failed"), RuntimeError("server failed")])
def test_main_web_reports_server_errors(monkeypatch, capsys, error):
    from pico_uart.web import app as web_app

    def fail(*_arguments):
        raise error

    monkeypatch.setattr(web_app, "run_server", fail)

    assert cli.main(["web"]) == 1
    assert str(error) in capsys.readouterr().err


def test_main_reports_hid_errors_and_status_timeouts(monkeypatch, capsys):
    class BrokenClient:
        def __init__(self, *_selectors):
            pass

        def __enter__(self):
            return self

        def __exit__(self, *_args):
            pass

        def read_board_temperature(self):
            raise OSError("HID disconnected")

    monkeypatch.setattr(cli, "PicoUartHid", BrokenClient)
    assert cli.main(["temperature"]) == 1
    assert "HID disconnected" in capsys.readouterr().err

    times = iter((0.0, 0.0, 0.0, 3.0))
    monkeypatch.setattr(cli.time, "monotonic", lambda: next(times))

    class EmptyClient(BrokenClient):
        def read_status(self, _timeout_ms):
            return None

    monkeypatch.setattr(cli, "PicoUartHid", EmptyClient)
    assert cli.main(["status", "--timeout", "2"]) == 1
    assert "timed out without receiving" in capsys.readouterr().err


def test_module_entrypoint_exits_with_cli_status(monkeypatch):
    monkeypatch.setattr(cli, "main", lambda: 7)

    with pytest.raises(SystemExit) as error:
        runpy.run_module("pico_uart.__main__", run_name="__main__")

    assert error.value.code == 7

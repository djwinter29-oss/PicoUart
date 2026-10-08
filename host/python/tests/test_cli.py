"""Hardware-free tests for the public PicoUart command-line interface."""

from __future__ import annotations

import runpy

import pytest

from helpers import status_report_bytes
from pico_uart import cli
from pico_uart.protocol import parse_status


@pytest.mark.parametrize(
    ("command", "expected_output", "expected_action"),
    [
        ("temperature", "temperature=23.50 C\n", "temperature"),
        ("version", "1.2.3\n", "version"),
        ("overruns", "cdc0=0 cdc1=1 cdc2=2 cdc3=3 cdc4=4 cdc5=5\n", "overruns"),
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

        def toggle_led(self):
            calls.append("toggle-led")

        def reset_board(self):
            calls.append("reset")

    monkeypatch.setattr(cli, "PicoUartHid", FakeClient)

    assert cli.main(["--serial", "board-1", command]) == 0
    assert capsys.readouterr().out == expected_output
    assert calls == [("selectors", "board-1", None), expected_action]


def test_main_status_text_retries_empty_reads_and_selects_device_path(monkeypatch, capsys):
    status = parse_status(status_report_bytes(sequence=17))
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
    assert capsys.readouterr().out.startswith("seq=17 ")


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

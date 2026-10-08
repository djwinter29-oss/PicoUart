from __future__ import annotations

import argparse
import importlib
import sys

import pytest


def _load_cli():
    module = importlib.import_module("hil_test_suite.cli")
    return importlib.reload(module)


def test_help_lists_available_commands(capsys) -> None:
    cli = _load_cli()

    with pytest.raises(SystemExit) as error:
        cli.main(["--help"])

    assert error.value.code == 0
    output = capsys.readouterr().out
    assert "functional" in output
    assert "performance" in output
    assert "bridge" in output


def test_no_arguments_shows_help(capsys) -> None:
    cli = _load_cli()

    assert cli.main([]) == 0
    assert "usage: pico-uart-hil" in capsys.readouterr().out


def test_subcommand_help_uses_command_arguments(capsys) -> None:
    cli = _load_cli()

    with pytest.raises(SystemExit) as error:
        cli.main(["functional", "--help"])

    assert error.value.code == 0
    output = capsys.readouterr().out
    assert "--pico-cdc0" in output
    assert "--stage" in output


def test_dispatches_parsed_subcommand_namespace(monkeypatch) -> None:
    cli = _load_cli()
    original_argv = ["pico-uart-hil"]
    observed = []
    monkeypatch.setattr(sys, "argv", original_argv)

    class FakeModule:
        @staticmethod
        def build_parser(add_help: bool = True):
            parser = argparse.ArgumentParser(add_help=add_help)
            parser.add_argument("--loopback", action="store_true")
            parser.add_argument("--skip-functional", action="store_true")
            return parser

        @staticmethod
        def main(arguments) -> int:
            observed.append((arguments.command, arguments.loopback, arguments.skip_functional))
            return 7

    monkeypatch.setattr(cli, "import_module", lambda _name, package: FakeModule)

    assert cli.main(["bridge", "--loopback"]) == 7
    assert observed == [("bridge", True, False)]
    assert sys.argv == original_argv


def test_options_without_command_dispatch_to_full(monkeypatch) -> None:
    cli = _load_cli()
    observed = []

    class FakeModule:
        @staticmethod
        def build_parser(add_help: bool = True):
            parser = argparse.ArgumentParser(add_help=add_help)
            parser.add_argument("--loopback", action="store_true")
            parser.add_argument("--skip-functional", action="store_true")
            return parser

        @staticmethod
        def main(arguments) -> int:
            observed.append((arguments.command, arguments.skip_functional))
            return 0

    monkeypatch.setattr(cli, "import_module", lambda _name, package: FakeModule)

    assert cli.main(["--skip-functional"]) == 0
    assert observed == [("full", True)]


def test_unknown_subcommand_fails(capsys) -> None:
    cli = _load_cli()

    with pytest.raises(SystemExit) as error:
        cli.main(["unknown"])

    assert error.value.code == 2
    assert "invalid choice" in capsys.readouterr().err
"""Single command-line entry point for PicoUart HIL tools."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from importlib import import_module

COMMAND_MODULES = {
    "full": "workflows.full",
    "functional": "workflows.functional",
    "performance": "workflows.performance",
    "bridge": "serial.bridge",
    "stress": "serial.stress",
    "pair": "serial.pair",
}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="pico-uart-hil",
        description="Run PicoUart hardware-in-the-loop checks.",
        epilog="Options without a subcommand are treated as options for 'full'.",
    )
    subparsers = parser.add_subparsers(dest="command")
    for command, module_name in COMMAND_MODULES.items():
        module = import_module(f".{module_name}", package=__package__)
        description = module.__doc__ or command
        command_parser = subparsers.add_parser(
            command,
            parents=[module.build_parser(add_help=False)],
            description=description,
            help=description.splitlines()[0],
        )
        command_parser.set_defaults(command_handler=module.main)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    parser = build_parser()

    if not arguments:
        parser.print_help()
        return 0

    if arguments[0].startswith("-") and arguments[0] not in ("-h", "--help"):
        arguments.insert(0, "full")

    parsed_arguments = parser.parse_args(arguments)
    command_handler = getattr(parsed_arguments, "command_handler", None)
    if command_handler is None:
        parser.print_help()
        return 0
    return command_handler(parsed_arguments)


if __name__ == "__main__":
    sys.exit(main())
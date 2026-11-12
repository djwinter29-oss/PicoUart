# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Seeded random-input checks: malformed reports may only raise RuntimeError, never crash the parsers."""

import contextlib
import random

import pytest

from pico_uart.hid import protocol

PARSERS = [
    (protocol.parse_device_info, protocol.DEVICE_INFO_SIZE),
    (protocol.parse_configuration, protocol.CONFIGURATION_SIZE),
    (protocol.parse_control, protocol.CONTROL_SIZE),
    (protocol.parse_telemetry, protocol.TELEMETRY_SIZE),
    (protocol.parse_channel, protocol.CHANNEL_SIZE),
    (protocol.parse_diagnostic, protocol.DIAGNOSTIC_SIZE),
]


@pytest.mark.parametrize(("parser", "size"), PARSERS, ids=[parser.__name__ for parser, _ in PARSERS])
def test_parsers_reject_random_payloads_only_with_runtime_error(parser, size):
    generator = random.Random(0x5049434F)
    for _ in range(2000):
        length = size if generator.random() < 0.8 else generator.randrange(0, size + 8)
        payload = bytes(generator.randrange(256) for _ in range(length))
        if length and generator.random() < 0.8:
            payload = bytes((protocol.LAYOUT_VERSION,)) + payload[1:]
        with contextlib.suppress(RuntimeError):
            parser(payload)

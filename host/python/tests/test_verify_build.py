#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import struct
from pathlib import Path

import pytest

VERIFY_BUILD = Path(__file__).resolve().parents[3] / "tools" / "linux" / "verify-build.py"


def _load_verifier():
    spec = importlib.util.spec_from_file_location("verify_build_under_test", VERIFY_BUILD)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _uf2_block(
    verifier,
    payload: bytes,
    target: int,
    number: int,
    count: int,
    family: int,
    flags: int = 0,
    extension: int | None = None,
) -> bytes:
    block = bytearray(512)
    struct.pack_into(
        "<IIIIIIII",
        block,
        0,
        verifier.UF2_MAGIC_START0,
        verifier.UF2_MAGIC_START1,
        verifier.UF2_FLAG_FAMILY_ID_PRESENT | flags,
        target,
        len(payload),
        number,
        count,
        family,
    )
    block[32 : 32 + len(payload)] = payload
    if extension is not None:
        struct.pack_into("<I", block, 32 + len(payload), extension)
    struct.pack_into("<I", block, 508, verifier.UF2_MAGIC_END)
    return bytes(block)


def test_uf2_payload_matches_binary_with_zero_padding() -> None:
    verifier = _load_verifier()
    binary = bytes(range(256)) + b"tail"
    uf2 = _uf2_block(verifier, binary[:256], 0x10000000, 0, 2, verifier.UF2_FAMILY_IDS["pico"])
    uf2 += _uf2_block(
        verifier,
        binary[256:] + bytes(252),
        0x10000100,
        1,
        2,
        verifier.UF2_FAMILY_IDS["pico"],
    )

    verifier._verify_uf2_payload(uf2, binary, verifier.UF2_FAMILY_IDS["pico"])


def test_pico2_uf2_accepts_separate_absolute_and_firmware_sequences() -> None:
    verifier = _load_verifier()
    binary = b"firmware"
    absolute = _uf2_block(
        verifier,
        b"\xef" * 256,
        verifier.RP2350_ABSOLUTE_BLOCK_ADDRESS,
        0,
        2,
        verifier.UF2_FAMILY_RP2350_ABSOLUTE,
        verifier.UF2_FLAG_EXTENSION_FLAGS_PRESENT,
        verifier.UF2_EXTENSION_RP2_IGNORE_BLOCK,
    )
    firmware = _uf2_block(
        verifier,
        binary,
        verifier.PICO_FLASH_BASE,
        0,
        1,
        verifier.UF2_FAMILY_IDS["pico2"],
    )

    verifier._verify_uf2_payload(
        absolute + firmware, binary, verifier.UF2_FAMILY_IDS["pico2"]
    )


def test_uf2_rejects_wrong_family() -> None:
    verifier = _load_verifier()
    uf2 = _uf2_block(verifier, b"firmware", 0x10000000, 0, 1, verifier.UF2_FAMILY_IDS["pico2"])

    with pytest.raises(ValueError, match="family ID"):
        verifier._verify_uf2_payload(uf2, b"firmware", verifier.UF2_FAMILY_IDS["pico"])


def test_uf2_rejects_shifted_flash_address() -> None:
    verifier = _load_verifier()
    uf2 = _uf2_block(verifier, b"firmware", 0x10001000, 0, 1, verifier.UF2_FAMILY_IDS["pico"])

    with pytest.raises(ValueError, match="unexpected address"):
        verifier._verify_uf2_payload(uf2, b"firmware", verifier.UF2_FAMILY_IDS["pico"])


def test_uf2_rejects_payload_mismatch() -> None:
    verifier = _load_verifier()
    uf2 = _uf2_block(verifier, b"firmware", 0x10000000, 0, 1, verifier.UF2_FAMILY_IDS["pico"])

    with pytest.raises(ValueError, match="differs"):
        verifier._verify_uf2_payload(uf2, b"firmwarX", verifier.UF2_FAMILY_IDS["pico"])


def test_uf2_rejects_missing_declared_block() -> None:
    verifier = _load_verifier()
    uf2 = _uf2_block(verifier, b"firmware", 0x10000000, 0, 2, verifier.UF2_FAMILY_IDS["pico"])

    with pytest.raises(ValueError, match="missing"):
        verifier._verify_uf2_payload(uf2, b"firmware", verifier.UF2_FAMILY_IDS["pico"])


def test_intel_hex_requires_final_canonical_eof() -> None:
    verifier = _load_verifier()

    assert verifier._intel_hex_load_base(b":01000000AA55\n:00000001FF\n") == 0

    with pytest.raises(ValueError, match="canonical EOF"):
        verifier._intel_hex_load_base(b":01000000AA55\n:00000101FE\n")


def test_intel_hex_rejects_records_after_eof() -> None:
    verifier = _load_verifier()

    with pytest.raises(ValueError, match="EOF"):
        verifier._intel_hex_load_base(b":00000001FF\n:01000000AA55\n")

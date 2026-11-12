#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Stage or serve the Direct Dashboard with a Git-derived host version."""

from __future__ import annotations

import argparse
from functools import partial
import html
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "host/direct/src"
VERSION = re.compile(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?"
                     r"(?:\+[0-9A-Za-z.-]+)?")
TAG_DESCRIPTION = re.compile(r"v(\d+)\.(\d+)\.(\d+)-(\d+)-g([0-9a-f]+)")
MARKER = 'aria-label="Host application version">0.0.0-debug</span>'


def git(root: Path, *args: str) -> str:
    return subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    ).stdout.strip()


def identity(root: Path) -> tuple[str, str, bool]:
    """Derive versions from vMAJOR.MINOR.PATCH tags, like the other hosts."""
    try:
        commit = git(root, "rev-parse", "HEAD")
    except (FileNotFoundError, subprocess.CalledProcessError) as error:
        print(f"Git identity unavailable; using debug version: {error}", file=sys.stderr)
        return "0.0.0-debug", "unknown", False

    dirty = bool(git(root, "status", "--porcelain", "--untracked-files=normal"))
    try:
        description = git(
            root,
            "describe",
            "--tags",
            "--long",
            "--match",
            "v[0-9]*.[0-9]*.[0-9]*",
            "--exclude",
            "v*-*",
            "--exclude",
            "v*+*",
        )
    except subprocess.CalledProcessError:
        version = f"0.0.0-dev.{git(root, 'rev-list', '--count', 'HEAD')}"
    else:
        match = TAG_DESCRIPTION.fullmatch(description)
        if match is None:
            raise RuntimeError(f"Unsupported version tag in Git description: {description}")
        major, minor, patch, height = map(int, match.groups()[:4])
        version = f"{major}.{minor}.{patch}" if height == 0 else f"{major}.{minor}.{patch + 1}-dev.{height}"

    return f"{version}+g{commit[:12]}" + (".dirty" if dirty else ""), commit, dirty


def build(source: Path, output: Path, version: str, commit: str, dirty: bool) -> None:
    """Copy the static app and stamp the copy, leaving canonical source untouched."""
    if VERSION.fullmatch(version) is None:
        raise ValueError(f"Invalid dashboard version: {version}")
    if output.resolve().is_relative_to(source.resolve()) or source.resolve().is_relative_to(output.resolve()):
        raise ValueError("Staging output must not overlap canonical source")
    if output.exists():
        raise FileExistsError(f"Staging output already exists: {output}")

    shutil.copytree(source, output)
    page = output / "index.html"
    text = page.read_text(encoding="utf-8")
    if text.count(MARKER) != 1:
        raise RuntimeError("Expected exactly one source version marker")
    label = f'aria-label="Host application version" title="{html.escape(commit, quote=True)}">'
    page.write_text(text.replace(MARKER, f"{label}{html.escape(version)}</span>"), encoding="utf-8")
    (output / "build.json").write_text(
        json.dumps({"version": version, "commit": commit, "dirty": dirty}, indent=2) + "\n",
        encoding="utf-8",
    )
    verify(output, version)


def verify(output: Path, expected: str) -> None:
    """Confirm the staged page contains the requested version and build identity."""
    metadata = json.loads((output / "build.json").read_text(encoding="utf-8"))
    page = (output / "index.html").read_text(encoding="utf-8")
    label = f'aria-label="Host application version" title="{html.escape(metadata["commit"], quote=True)}">'
    if metadata["version"] != expected or f"{label}{html.escape(expected)}</span>" not in page:
        raise RuntimeError("Staged dashboard version differs from the requested version")
    if MARKER in page:
        raise RuntimeError("Staged dashboard still contains its debug version marker")


def serve(port: int) -> None:
    version, commit, dirty = identity(ROOT)
    with TemporaryDirectory(prefix="pico-uart-direct-") as directory:
        output = Path(directory) / "site"
        build(SOURCE, output, version, commit, dirty)
        server = ThreadingHTTPServer(("127.0.0.1", port), partial(SimpleHTTPRequestHandler, directory=output))
        print(f"Direct Dashboard {version} at http://127.0.0.1:{server.server_port}/", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build", help="stage the dashboard with an explicit version")
    build_parser.add_argument("--output", type=Path, required=True)
    build_parser.add_argument("--version", required=True)
    verify_parser = commands.add_parser("verify", help="verify a staged dashboard version")
    verify_parser.add_argument("--output", type=Path, required=True)
    verify_parser.add_argument("--version", required=True)
    serve_parser = commands.add_parser("serve", help="serve a temporary Git-versioned dashboard")
    serve_parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    if args.command == "build":
        _, commit, dirty = identity(ROOT)
        build(SOURCE, args.output.resolve(), args.version, commit, dirty)
    elif args.command == "verify":
        verify(args.output.resolve(), args.version)
    else:
        if not 1 <= args.port <= 65535:
            parser.error("port must be from 1 to 65535")
        serve(args.port)


if __name__ == "__main__":
    main()

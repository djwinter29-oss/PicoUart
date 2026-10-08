#!/usr/bin/env python3
"""Filter excluded packages and exclusive dependencies from a uv lock file."""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import sys


PACKAGE_LINE = re.compile(r"^([A-Za-z0-9_.-]+)==")
VIA_LINE = re.compile(r"^[ \t]*# via[ \t]+(.+?)\s*$")


@dataclass
class PackageBlock:
    name: str
    lines: list[str] = field(default_factory=list)
    parents: list[str] = field(default_factory=list)


def _normalize_name(name: str) -> str:
    return re.sub(r"[-_.]+", "-", name.strip()).casefold()


def filter_lock_text(lock_text: str, excluded_names: set[str]) -> str:
    """Drop excluded packages and dependencies required only by those packages."""
    excluded = {_normalize_name(name) for name in excluded_names}
    preamble: list[str] = []
    packages: list[PackageBlock] = []
    current: PackageBlock | None = None

    for line in lock_text.splitlines(keepends=True):
        package_match = PACKAGE_LINE.match(line)
        if package_match:
            if current is not None:
                packages.append(current)
            current = PackageBlock(_normalize_name(package_match.group(1)), [line])
            continue

        if current is None:
            preamble.append(line)
            continue

        current.lines.append(line)
        via_match = VIA_LINE.match(line.rstrip("\r\n"))
        if via_match:
            current.parents.append(_normalize_name(via_match.group(1)))

    if current is not None:
        packages.append(current)
    if not packages:
        raise ValueError("requirements lock contains no package entries")

    missing = excluded - {package.name for package in packages}
    if missing:
        raise ValueError(f"excluded package(s) not found in lock: {', '.join(sorted(missing))}")

    changed = True
    while changed:
        changed = False
        for package in packages:
            if package.name in excluded or not package.parents:
                continue
            if all(parent in excluded for parent in package.parents):
                excluded.add(package.name)
                changed = True

    output = list(preamble)
    for package in packages:
        if package.name in excluded:
            continue
        for line in package.lines:
            via_match = VIA_LINE.match(line.rstrip("\r\n"))
            if via_match and _normalize_name(via_match.group(1)) in excluded:
                continue
            output.append(line)
    return "".join(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exclude", required=True, help="comma-separated package names to remove")
    parser.add_argument("lock_file", help="lock file path, or - to read standard input")
    arguments = parser.parse_args()
    excluded = {name.strip() for name in arguments.exclude.split(",") if name.strip()}
    try:
        if not excluded:
            raise ValueError("at least one package name must be excluded")
        if arguments.lock_file == "-":
            lock_text = sys.stdin.read()
        else:
            lock_text = Path(arguments.lock_file).read_text(encoding="utf-8")
        sys.stdout.write(filter_lock_text(lock_text, excluded))
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

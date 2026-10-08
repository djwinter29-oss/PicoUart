"""Published byte ranges must cover every direction/run in the linked logs."""

import ast
import re


def test_isolated_pair_summary_covers_raw_evidence(repo_root):
    raw = repo_root / "docs/tests/raw/hardware-test-2026-10-01-cc121e8"
    # UTF-8 is required: the summary uses en-dashes in baud ranges. Windows
    # locale encoding would mangle those and shrink claimed ranges.
    document = (repo_root / "docs/tests/hardware-validation-2026-10-01.md").read_text(
        encoding="utf-8"
    )
    for rate in (115200, 230400, 460800):
        single = []
        duplex = {}
        for stage in ("stage2", "stage3"):
            duplex[stage] = []
            for direction in ("a-to-b", "b-to-a", "both"):
                lines = (raw / f"{stage}-{direction}-{rate}.log").read_text(
                    encoding="utf-8"
                ).splitlines()
                assert len(lines) == 3
                for line in lines:
                    assert " PASS " in line
                    results = ast.literal_eval(line.split(" PASS ", 1)[1])
                    assert set(results) == ({"a-to-b", "b-to-a"} if direction == "both" else {direction})
                    for count, error in results.values():
                        assert error is None
                        (duplex[stage] if direction == "both" else single).append(count)
        row = next(line for line in document.splitlines() if line.startswith(f"| {rate} |"))
        cells = row.split("|")

        def claimed_range(text):
            numbers = [int(value.replace(",", "")) for value in re.findall(r"[\d,]+", text)]
            return min(numbers), max(numbers)

        assert claimed_range(cells[2]) == (min(single), max(single))
        if rate == 460800:
            claims = dict(re.findall(r"stage (\d): ([\d,–]+)", cells[3]))
            for stage in ("stage2", "stage3"):
                assert claimed_range(claims[stage[-1]]) == (min(duplex[stage]), max(duplex[stage]))
        else:
            counts = duplex["stage2"] + duplex["stage3"]
            assert claimed_range(cells[3]) == (min(counts), max(counts))

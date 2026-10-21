"""Fail publication unless the selected commit's latest required workflow runs passed."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from urllib.parse import quote


def require_successful_run(repository: str, workflow: str, sha: str, tag: str | None = None) -> None:
    """Require the latest matching run, including its latest attempt, to have succeeded."""
    result = subprocess.run(
        ["gh", "api", "--paginate", "--slurp",
         f"repos/{repository}/actions/workflows/{workflow}/runs?head_sha={sha}&per_page=100"],
        check=True, capture_output=True, text=True,
    )
    runs = [run for page in json.loads(result.stdout) for run in page["workflow_runs"]
            if run["head_sha"] == sha and run["event"] in ("push", "pull_request")
            and (tag is None or (run["event"] == "push" and run["head_branch"] == tag))]
    if not runs:
        raise RuntimeError(f"{workflow}: no required test run found for {sha}")
    latest = max(runs, key=lambda run: (run["run_number"], run["run_attempt"]))
    if latest["status"] != "completed" or latest["conclusion"] != "success":
        raise RuntimeError(f"{workflow}: latest run is {latest['status']}/{latest['conclusion']}; "
                           f"publication blocked: {latest['html_url']}")
    print(f"Passed {workflow}: {latest['html_url']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--tag", help="Release tag requiring PR and release validation")
    selection.add_argument("--sha", help="Test deployment commit requiring PR validation")
    arguments = parser.parse_args()
    repository = os.environ["GITHUB_REPOSITORY"]
    try:
        result = subprocess.run(
            ["gh", "api", f"repos/{repository}/commits/{quote(arguments.tag or arguments.sha, safe='')}"],
            check=True, capture_output=True, text=True,
        )
        sha = json.loads(result.stdout)["sha"]
        require_successful_run(repository, "pr-check.yml", sha)
        if arguments.tag:
            require_successful_run(repository, "release.yml", sha, arguments.tag)
        if output_file := os.environ.get("GITHUB_OUTPUT"):
            with open(output_file, "a", encoding="utf-8") as output:
                output.write(f"sha={sha}\n")
    except (subprocess.CalledProcessError, RuntimeError, KeyError, ValueError) as error:
        print(f"Required tests have not passed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
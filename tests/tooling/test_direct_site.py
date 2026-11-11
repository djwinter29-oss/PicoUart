# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
"""Tests for Git-based Direct Dashboard versioning and staging."""

import subprocess

import pytest

from tools.host import direct_site


def _git(root, *args):
    return subprocess.run(
        ["git", "-C", str(root), *args],
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
    ).stdout.strip()


def _commit(root, path, content):
    (root / path).write_text(content, encoding="utf-8")
    _git(root, "add", path)
    _git(root, "-c", "user.name=Build Test", "-c", "user.email=build@example.invalid",
         "-c", "commit.gpgsign=false", "commit", "-m", content)


@pytest.fixture
def versioned_repository(tmp_path):
    _git(tmp_path, "init", "-q")
    _commit(tmp_path, "version.txt", "first")
    _git(tmp_path, "tag", "v1.2.3")
    return tmp_path


def test_direct_version_uses_release_tag_and_commit_height(versioned_repository):
    version, commit, dirty = direct_site.identity(versioned_repository)
    assert version == f"1.2.3+g{commit[:12]}"
    assert not dirty

    _commit(versioned_repository, "version.txt", "second")
    version, commit, dirty = direct_site.identity(versioned_repository)
    assert version == f"1.2.4-dev.1+g{commit[:12]}"
    assert not dirty

    (versioned_repository / "untracked.txt").write_text("dirty", encoding="utf-8")
    version, commit, dirty = direct_site.identity(versioned_repository)
    assert version == f"1.2.4-dev.1+g{commit[:12]}.dirty"
    assert dirty


def test_direct_version_without_tags_uses_development_identity(tmp_path):
    _git(tmp_path, "init", "-q")
    _commit(tmp_path, "version.txt", "first")
    version, commit, dirty = direct_site.identity(tmp_path)
    assert version == f"0.0.0-dev.1+g{commit[:12]}"
    assert not dirty


def test_direct_site_build_stamps_only_staged_copy(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    original = f"<span {direct_site.MARKER}>"
    (source / "index.html").write_text(original, encoding="utf-8")
    output = tmp_path / "staged"

    direct_site.build(source, output, "1.2.3", "abcdef0123456789", False)
    direct_site.verify(output, "1.2.3")

    assert (source / "index.html").read_text(encoding="utf-8") == original
    page = (output / "index.html").read_text(encoding="utf-8")
    assert 'title="abcdef0123456789">1.2.3</span>' in page
    assert (output / "build.json").is_file()

    with pytest.raises(FileExistsError):
        direct_site.build(source, output, "1.2.3", "abcdef0123456789", False)


def test_direct_site_rejects_invalid_versions(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    (source / "index.html").write_text(direct_site.MARKER, encoding="utf-8")

    with pytest.raises(ValueError, match="Invalid dashboard version"):
        direct_site.build(source, tmp_path / "staged", "<script>", "unknown", False)

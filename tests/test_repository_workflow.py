#!/usr/bin/env python3
"""Repository-level checks for the Android build and release workflow."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / ".github/workflows/android-apk.yml").read_text(encoding="utf-8")
GRADLE = (ROOT / "app/build.gradle.kts").read_text(encoding="utf-8")
BUILD_DOC = (ROOT / "docs/ANDROID_BUILD_AND_RELEASE.md").read_text(encoding="utf-8")


def test_pull_requests_build_and_generated_assets_are_verified():
    assert "pull_request:" in WORKFLOW
    assert "branches:\n      - main" in WORKFLOW
    assert "Verify deterministic audio, catalog and atlas" in WORKFLOW
    assert "git diff --cached --quiet" in WORKFLOW
    assert "git push origin HEAD:main" not in WORKFLOW
    assert "contents: read" in WORKFLOW


def test_write_permission_is_isolated_to_tag_prerelease_job():
    assert "draft-prerelease:" in WORKFLOW
    release_job = WORKFLOW[WORKFLOW.index("  draft-prerelease:"):]
    assert "if: startsWith(github.ref, 'refs/tags/v')" in release_job
    assert "contents: write" in release_job
    build_job = WORKFLOW[WORKFLOW.index("  build-apk:"):WORKFLOW.index("  draft-prerelease:")]
    assert "contents: write" not in build_job


def test_android_version_and_release_documentation_match():
    assert "versionCode = 26" in GRADLE
    assert 'versionName = "2.2.1"' in GRADLE
    assert "versionCode = 26" in BUILD_DOC
    assert "versionName = 2.2.1" in BUILD_DOC


if __name__ == "__main__":
    test_pull_requests_build_and_generated_assets_are_verified()
    test_write_permission_is_isolated_to_tag_prerelease_job()
    test_android_version_and_release_documentation_match()
    print("Minefade repository workflow checks passed.")

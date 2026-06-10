#!/usr/bin/env python3
"""Fetch and report external VOGUE dependencies.

Reads config/deps.json.  The top-level sections are:

  kraftkit.packages  — KraftKit package-registry libraries (musl, libcxx, …).
                       Resolved at `kraft build` time; deps.py only registers
                       the manifest and refreshes the index.

  git_sources        — Repos that must be cloned locally before building.
                       Each entry has: path, repo, rev.
                       Optional fields:
                         env        — environment variable that overrides `path`
                         env_suffix — appended to `path` when deriving env value
                                      (e.g. "/include" for header-only checkouts)
                         patches    — list of repo-root-relative .patch files
                                      re-applied (idempotently) after every
                                      checkout/refresh, so local fixes to a
                                      pinned upstream survive deps-refresh.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config" / "deps.json"


def load_config() -> dict:
    return json.loads(CONFIG.read_text())


def resolved_path(entry: dict) -> Path:
    """Return the local clone path for a git_sources entry.

    If the entry has an 'env' key and that variable is set, use it (stripping
    any env_suffix so the returned path is always the repo root, not a subdir).
    Otherwise fall back to ROOT / entry['path'].
    """
    env_key = entry.get("env")
    suffix = entry.get("env_suffix", "")
    if env_key:
        override = os.environ.get(env_key)
        if override:
            p = Path(override).expanduser().resolve()
            if suffix and str(p).endswith(suffix):
                p = Path(str(p)[: -len(suffix)])
            return p
    return (ROOT / entry["path"]).resolve()


def run(command: list[str], *, check: bool = True) -> int:
    print("+", " ".join(command))
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if check and completed.returncode != 0:
        raise SystemExit(completed.returncode)
    return completed.returncode


def ensure_git_checkout(path: Path, repo: str, rev: str, ref_type: str, refresh: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        if ref_type == "commit":
            # Shallow clone of default branch, then fetch the specific commit.
            # GitHub and GitLab allow fetching reachable SHAs shallowly.
            run(["git", "clone", "--depth=1", "--single-branch", repo, str(path)])
            run(["git", "-C", str(path), "fetch", "--depth=1", "origin", rev])
            run(["git", "-C", str(path), "checkout", "FETCH_HEAD"])
        elif ref_type == "tag":
            run(["git", "clone", "--depth=1", "--single-branch", "-b", rev, repo, str(path)])
        else:  # branch
            run(["git", "clone", "--depth=1", "--single-branch", "-b", rev, repo, str(path)])
    elif refresh:
        if ref_type == "commit":
            run(["git", "-C", str(path), "fetch", "--depth=1", "origin", rev])
            run(["git", "-C", str(path), "checkout", "FETCH_HEAD"])
        elif ref_type == "tag":
            run(["git", "-C", str(path), "fetch", "--depth=1", "origin",
                 f"refs/tags/{rev}:refs/tags/{rev}"])
            run(["git", "-C", str(path), "checkout", rev])
        else:  # branch
            run(["git", "-C", str(path), "fetch", "--depth=1", "origin",
                 f"refs/heads/{rev}:refs/remotes/origin/{rev}"])
            run(["git", "-C", str(path), "checkout", rev])


def apply_patches(path: Path, patches: list[str]) -> None:
    """Apply repo-tracked patches to a checkout, idempotently.

    A patch that already applies in reverse is treated as present and skipped,
    so this is safe to run on both fresh clones and existing checkouts.
    """
    for patch in patches:
        patch_abs = ROOT / patch
        already = subprocess.run(
            ["git", "-C", str(path), "apply", "--reverse", "--check", str(patch_abs)],
            cwd=ROOT, check=False, capture_output=True,
        )
        if already.returncode == 0:
            print(f"= patch already applied: {patch}")
            continue
        run(["git", "-C", str(path), "apply", str(patch_abs)])


def fetch(refresh: bool) -> None:
    config = load_config()

    # Register the KraftKit manifest and refresh the package index so that
    # `kraft build` can resolve packages (musl, libcxx, …) at build time.
    manifest = config["kraftkit"]["manifest"]
    run(["kraft", "pkg", "source", manifest], check=False)
    run(["kraft", "pkg", "update"])

    # Clone every git_source declared in config/deps.json, then
    # re-apply any local patches it carries.
    for name, entry in config["git_sources"].items():
        path = resolved_path(entry)
        ensure_git_checkout(path, entry["repo"], entry["rev"], entry["ref_type"], refresh)
        if entry.get("patches"):
            apply_patches(path, entry["patches"])


def status() -> int:
    config = load_config()
    missing = False

    print("KraftKit manifest:", config["kraftkit"]["manifest"])
    print("KraftKit packages:")
    for name, version in config["kraftkit"]["packages"].items():
        print(f"  {name}: {version}")

    print("Git sources:")
    for name, entry in config["git_sources"].items():
        path = resolved_path(entry)
        suffix = entry.get("env_suffix", "")
        env_key = entry.get("env")
        env_note = f" (via ${env_key})" if env_key and os.environ.get(env_key) else ""
        state = "ok" if path.exists() else "MISSING"
        print(f"  {name}: {path}{suffix}{env_note} [{state}]")
        if not path.exists():
            missing = True
            continue
        completed = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            cwd=ROOT, check=False, capture_output=True, text=True,
        )
        if completed.returncode == 0:
            print(f"    HEAD={completed.stdout.strip()[:12]}  expected={entry['rev']}")
        else:
            print("    not a git checkout")
            missing = True

    return 1 if missing else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    fetch_parser = sub.add_parser("fetch", help="Clone / update all git_sources")
    fetch_parser.add_argument("--refresh", action="store_true",
                              help="Re-fetch even if the checkout already exists")
    sub.add_parser("status", help="Show state of all git_sources")

    args = parser.parse_args()
    if args.command == "fetch":
        fetch(args.refresh)
        return 0
    if args.command == "status":
        return status()
    raise AssertionError("unreachable")


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Fetch and report external VOGUE dependencies."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "config" / "external_paths.json"


def load_config() -> dict:
    return json.loads(CONFIG.read_text())


def rel_default(entry: dict) -> Path:
    default = entry.get("default", "")
    if not default:
        raise ValueError("entry has no default path")
    return ROOT / default


def resolved_path(entry: dict) -> Path | None:
    override = os.environ.get(entry["env"])
    if override:
        return Path(override).expanduser().resolve()
    default = entry.get("default", "")
    if not default:
        return None
    return rel_default(entry).resolve()


def is_optional(entry: dict) -> bool:
    return not entry.get("default")


def run(command: list[str], *, check: bool = True) -> int:
    print("+", " ".join(command))
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if check and completed.returncode != 0:
        raise SystemExit(completed.returncode)
    return completed.returncode


def ensure_git_checkout(path: Path, repo: str, rev: str, refresh: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        run(["git", "clone", repo, str(path)])
    elif refresh:
        run(["git", "-C", str(path), "fetch", "--tags", "origin"])
    run(["git", "-C", str(path), "checkout", rev])


def fetch(refresh: bool) -> None:
    config = load_config()
    manifest = config["kraftkit"]["manifest"]
    run(["kraft", "pkg", "source", manifest], check=False)
    run(["kraft", "pkg", "update"])

    for entry in config["paths"].values():
        git_entry = entry.get("git")
        if not git_entry:
            continue
        path = resolved_path(entry)
        if path is None:
            raise SystemExit(f"{entry['env']} has no default path; set the environment variable before fetch")
        ensure_git_checkout(
            path,
            git_entry["repo"],
            git_entry["rev"],
            refresh,
        )

    kraftfiles = [
        "Kraftfile",
        "kraft/Kraftfile.kmscube-vgpu-gl",
        "kraft/Kraftfile.llama-cpu",
        "kraft/Kraftfile.llama-cpu-bench",
        "kraft/Kraftfile.llama-cpu-server",
        "kraft/Kraftfile.llama-vk",
        "kraft/Kraftfile.llama-vk-server",
    ]
    for kraftfile in kraftfiles:
        command = ["kraft", "fetch", "--kraftfile", kraftfile]
        if refresh:
            command += ["--no-cache"]
        run(command)


def status() -> int:
    config = load_config()
    missing = False
    print("Kraft manifest:", config["kraftkit"]["manifest"])
    print("Pinned Unikraft:", config["kraftkit"]["unikraft"]["version"])
    print("Official libraries:")
    for name, version in config["kraftkit"]["libraries"].items():
        print(f"  {name}: {version}")

    print("External Git sources:")
    for name, entry in config["paths"].items():
        path = resolved_path(entry)
        if path is None:
            print(f"  {name}: [unset optional]")
            continue
        state = "ok" if path.exists() else "missing"
        print(f"  {name}: {path} [{state}]")
        if not path.exists():
            if not is_optional(entry):
                missing = True
            continue
        git_entry = entry.get("git")
        if git_entry:
            completed = subprocess.run(
                ["git", "-C", str(path), "rev-parse", "HEAD"],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            if completed.returncode == 0:
                print(f"    HEAD={completed.stdout.strip()} expected={git_entry['rev']}")
            else:
                print("    not a Git checkout")
                missing = True
    return 1 if missing else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)

    fetch_parser = sub.add_parser("fetch")
    fetch_parser.add_argument("--refresh", action="store_true")
    sub.add_parser("status")

    args = parser.parse_args()
    if args.command == "fetch":
        fetch(args.refresh)
        return 0
    if args.command == "status":
        return status()
    raise AssertionError("unreachable")


if __name__ == "__main__":
    raise SystemExit(main())

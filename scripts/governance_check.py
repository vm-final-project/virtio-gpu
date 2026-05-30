#!/usr/bin/env python3
"""Validate VOGUE research-artifact governance metadata."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
GOV = ROOT / "config" / "governance.json"
MANIFEST_ROOT = ROOT.parent / "manifest"
DOC = ROOT / "docs" / "GOVERNANCE.md"
ALLOWED_APP_STATUSES = {"canonical", "benchmark", "experimental", "deprecated", "demo"}
ALLOWED_LIB_STABILITY = {"stable", "stable-small", "semi-stable", "experimental", "bounded-unstable"}


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return ""


def load_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise AssertionError(f"missing {path.relative_to(ROOT)}")
    except json.JSONDecodeError as exc:
        raise AssertionError(f"invalid JSON in {path.relative_to(ROOT)}: {exc}") from exc
    if not isinstance(data, dict):
        raise AssertionError(f"{path.relative_to(ROOT)} must contain a JSON object")
    return data


def make_targets() -> set[str]:
    targets: set[str] = set()
    for line in read(ROOT / "Makefile").splitlines():
        if not line or line.startswith("\t") or line.startswith("#"):
            continue
        # Keep grouped aliases: "test tests: ...".
        lhs = line.split(":", 1)[0]
        if not re.match(r"^[A-Za-z0-9_. -]+$", lhs):
            continue
        for name in lhs.split():
            if re.match(r"^[A-Za-z0-9_.-]+$", name):
                targets.add(name)
    return targets


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def check_apps(gov: dict[str, Any], targets: set[str], errors: list[str]) -> None:
    apps = gov.get("apps")
    require(isinstance(apps, dict), "config/governance.json: apps must be an object", errors)
    if not isinstance(apps, dict):
        return
    actual = {p.name for p in (ROOT / "apps").iterdir() if p.is_dir() and not p.name.startswith(".")}
    declared = set(apps)
    require(actual == declared,
            f"apps metadata mismatch missing={sorted(actual - declared)} stale={sorted(declared - actual)}",
            errors)
    for name, meta in sorted(apps.items()):
        if not isinstance(meta, dict):
            errors.append(f"apps.{name}: metadata must be an object")
            continue
        status = meta.get("status")
        require(status in ALLOWED_APP_STATUSES, f"apps.{name}: invalid status {status!r}", errors)
        require(bool(str(meta.get("purpose", "")).strip()), f"apps.{name}: missing purpose", errors)
        verifications = meta.get("verification")
        require(isinstance(verifications, list) and verifications, f"apps.{name}: verification must be a non-empty list", errors)
        if isinstance(verifications, list):
            missing = [t for t in verifications if t not in targets]
            require(not missing, f"apps.{name}: unknown verification targets {missing}", errors)
        rows = meta.get("evidence_rows")
        require(isinstance(rows, list) and rows, f"apps.{name}: evidence_rows must be non-empty", errors)


def check_libraries(gov: dict[str, Any], errors: list[str]) -> None:
    libs = gov.get("libraries")
    require(isinstance(libs, dict), "config/governance.json: libraries must be an object", errors)
    if not isinstance(libs, dict):
        return
    actual = {p.name for p in (ROOT / "libs").iterdir() if p.is_dir() and not p.name.startswith(".")}
    declared = set(libs)
    require(actual == declared,
            f"library metadata mismatch missing={sorted(actual - declared)} stale={sorted(declared - actual)}",
            errors)
    for name, meta in sorted(libs.items()):
        if not isinstance(meta, dict):
            errors.append(f"libraries.{name}: metadata must be an object")
            continue
        libdir = ROOT / "libs" / name
        require(bool(str(meta.get("owner", "")).strip()), f"libraries.{name}: missing owner", errors)
        require(meta.get("api_stability") in ALLOWED_LIB_STABILITY,
                f"libraries.{name}: invalid api_stability {meta.get('api_stability')!r}", errors)
        require(bool(str(meta.get("boundary", "")).strip()), f"libraries.{name}: missing boundary", errors)
        headers = meta.get("public_headers")
        require(isinstance(headers, list), f"libraries.{name}: public_headers must be a list", errors)
        if isinstance(headers, list):
            for header in headers:
                require((libdir / str(header)).exists(), f"libraries.{name}: missing public header {header}", errors)
        require((libdir / "README.md").exists(), f"libraries.{name}: missing README.md", errors)


def check_taxonomy(gov: dict[str, Any], targets: set[str], errors: list[str]) -> None:
    taxonomy = gov.get("make_target_taxonomy")
    require(isinstance(taxonomy, dict), "make_target_taxonomy must be an object", errors)
    if not isinstance(taxonomy, dict):
        return
    for target in ["test-fast", "test-native", "test-qemu", "test-gpu", "eval", "verify"]:
        require(target in taxonomy, f"make_target_taxonomy missing {target}", errors)
        require(target in targets, f"Makefile missing governance target {target}", errors)
    for target, meta in taxonomy.items():
        if not isinstance(meta, dict):
            errors.append(f"make_target_taxonomy.{target}: metadata must be an object")
            continue
        require(bool(str(meta.get("purpose", "")).strip()), f"make_target_taxonomy.{target}: missing purpose", errors)
        req = meta.get("required_targets")
        require(isinstance(req, list) and req, f"make_target_taxonomy.{target}: required_targets must be non-empty", errors)
        if isinstance(req, list):
            missing = [str(t) for t in req if str(t) not in targets]
            require(not missing, f"make_target_taxonomy.{target}: unknown required targets {missing}", errors)


def resolve_declared_path(value: str) -> Path:
    path = Path(value)
    return (ROOT / path).resolve() if not path.is_absolute() else path


def check_external_manifest(gov: dict[str, Any], errors: list[str]) -> None:
    manifest = gov.get("external_manifest")
    require(isinstance(manifest, dict), "external_manifest must describe the project manifest directory", errors)
    if not isinstance(manifest, dict):
        return
    require(resolve_declared_path(str(manifest.get("path", ""))) == MANIFEST_ROOT.resolve(),
            "external_manifest.path must be ../manifest", errors)
    require(bool(str(manifest.get("purpose", "")).strip()), "external_manifest missing purpose", errors)
    require(bool(str(manifest.get("vm_final_project_boundary", "")).strip()),
            "external_manifest missing vm_final_project_boundary", errors)
    required = manifest.get("required_files")
    require(isinstance(required, list) and required, "external_manifest.required_files must be non-empty", errors)
    if isinstance(required, list):
        for item in required:
            path = resolve_declared_path(str(item))
            require(path.exists(), f"external_manifest missing required file {item}", errors)
    require(not (ROOT / "config" / "artifact-manifest-lock.json").exists(),
            "vm-final-project must not carry duplicate config/artifact-manifest-lock.json; use ../manifest", errors)
    require(not (ROOT / "manifest").exists(),
            "vm-final-project must not carry duplicate manifest/ directory; use sibling ../manifest", errors)
    vm_runner_dupes = [p for p in (ROOT / "scripts").glob("exp-*") if p.is_file()]
    require(not vm_runner_dupes,
            f"vm-final-project must not duplicate manifest runner scripts: {[p.name for p in vm_runner_dupes]}", errors)
    manifest_script_names = {p.name for p in (MANIFEST_ROOT / "scripts").glob("*") if p.is_file()}
    vm_eval_names = {p.name for p in (ROOT / "scripts").glob("*.py")
                     if any(token in p.name for token in ["eval", "perf", "bench", "test"])}
    overlaps = sorted(manifest_script_names & vm_eval_names)
    require(not overlaps, f"manifest must not duplicate VM test/perf/eval scripts: {overlaps}", errors)


def check_claims(gov: dict[str, Any], targets: set[str], errors: list[str]) -> None:
    claims = gov.get("release_claims")
    require(isinstance(claims, list) and claims, "release_claims must be a non-empty list", errors)
    if not isinstance(claims, list):
        return
    for claim in claims:
        if not isinstance(claim, dict):
            errors.append("release_claims entry must be an object")
            continue
        claim_id = claim.get("id", "<unknown>")
        for key in ["id", "grade", "command", "result_file", "manifest", "rule"]:
            require(bool(str(claim.get(key, "")).strip()), f"release_claims.{claim_id}: missing {key}", errors)
        if claim.get("grade") == "release":
            require(bool(str(claim.get("experiment_spec", "")).strip()),
                    f"release_claims.{claim_id}: release claims must name a manifest experiment_spec", errors)
        require(claim.get("grade") in {"release", "non-release"},
                f"release_claims.{claim_id}: grade must be release or non-release", errors)
        command = str(claim.get("command", ""))
        if command.startswith("make "):
            target = command.split()[1]
            require(target in targets, f"release_claims.{claim_id}: unknown make target {target}", errors)
        manifest = resolve_declared_path(str(claim.get("manifest", "")))
        require(manifest.exists(), f"release_claims.{claim_id}: manifest missing {claim.get('manifest')}", errors)
        if claim.get("experiment_spec"):
            spec = resolve_declared_path(str(claim.get("experiment_spec")))
            require(spec.exists(), f"release_claims.{claim_id}: experiment_spec missing {claim.get('experiment_spec')}", errors)


def check_docs(errors: list[str]) -> None:
    doc = read(DOC)
    readme = read(ROOT / "README.md")
    require("Developer workflow split" in doc, "docs/GOVERNANCE.md missing Developer workflow split", errors)
    for phrase in ["make test-fast", "make test-native", "make test-qemu", "make test-gpu", "make governance-check"]:
        require(phrase in doc or phrase in readme, f"governance docs missing {phrase}", errors)
    require("../manifest" in doc and "../manifest" in readme, "docs must identify ../manifest as reproducibility owner", errors)
    require("docs/GOVERNANCE.md" in readme, "README must link docs/GOVERNANCE.md", errors)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="fail on governance drift")
    args = parser.parse_args()
    errors: list[str] = []
    try:
        gov = load_json(GOV)
    except AssertionError as exc:
        errors.append(str(exc))
        gov = {}
    targets = make_targets()
    check_taxonomy(gov, targets, errors)
    check_apps(gov, targets, errors)
    check_libraries(gov, errors)
    check_external_manifest(gov, errors)
    check_claims(gov, targets, errors)
    check_docs(errors)

    if errors:
        print("governance_check: FAIL", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    app_count = len(gov.get("apps", {})) if isinstance(gov.get("apps"), dict) else 0
    lib_count = len(gov.get("libraries", {})) if isinstance(gov.get("libraries"), dict) else 0
    print(f"governance_check: PASS apps={app_count} libs={lib_count} targets={len(targets)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

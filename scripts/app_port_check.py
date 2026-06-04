#!/usr/bin/env python3
"""Static checks for VOGUE official-source application ports."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REQUIRED_PORTING_SECTIONS = [
    "## Upstream provenance",
    "## Evidence",
    "## Porting boundary",
    "## Unikraft build system",
    "## Claim boundaries",
    "## Verification",
]

APPS = {
}


def read(path: Path) -> str:
    try:
        return path.read_text()
    except FileNotFoundError:
        raise AssertionError(f"missing {path.relative_to(ROOT)}")


def require(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def check_app(name: str, meta: dict[str, object]) -> None:
    appdir = meta["dir"]  # type: ignore[assignment]
    assert isinstance(appdir, Path)
    for fname in ["Config.uk", "Makefile.uk", "exportsyms.uk", "main.c", "PORTING.md"]:
        require((appdir / fname).exists(), f"{name}: missing {fname}")

    mk = read(appdir / "Makefile.uk")
    cfg = read(appdir / "Config.uk")
    syms = read(appdir / "exportsyms.uk").strip().splitlines()
    porting = read(appdir / "PORTING.md")
    main = read(appdir / "main.c")
    kraft = read(meta["kraft"])  # type: ignore[arg-type]

    prefix = str(meta["prefix"])
    config = str(meta["config"])
    require("$(eval $(call addlib,app" in mk, f"{name}: Makefile.uk lacks addlib registration")
    require(f"{prefix}_SRCS-$({config})" in mk, f"{name}: Makefile.uk lacks namespaced source list")
    require(f"{prefix}_UPSTREAM_REPO" in mk and str(meta["repo"]) in mk, f"{name}: missing upstream repo metadata")
    require(str(meta["ref"]) in mk and str(meta["ref"]) in porting, f"{name}: missing pinned upstream ref")
    require(re.search(rf"config\s+{config.removeprefix('CONFIG_')}\b", cfg), f"{name}: Config.uk lacks symbol")
    require("select LIBUK" in cfg, f"{name}: Config.uk lacks dependency selection")
    require(syms == ["main"], f"{name}: exportsyms.uk should export only main")
    require(str(meta["repo"]) in porting and str(meta["allowed"]) in porting, f"{name}: PORTING.md lacks provenance/evidence row")
    require(str(meta["ref"]).split()[0] in main or "UPSTREAM_REF" in main, f"{name}: main.c lacks upstream marker")
    require(config in kraft and f"app-{name}" in kraft, f"{name}: standalone Kraftfile does not select app")
    require("CONFIG_APP_KMSCUBE" not in kraft, f"{name}: standalone Kraftfile selects kmscube too")


def declared_apps() -> dict[str, object]:
    try:
        gov = json.loads((ROOT / "config" / "governance.json").read_text())
    except (FileNotFoundError, json.JSONDecodeError) as exc:
        raise AssertionError(f"cannot load config/governance.json: {exc}") from exc
    apps = gov.get("apps")
    require(isinstance(apps, dict) and bool(apps), "config/governance.json apps must be a non-empty object")
    return apps  # type: ignore[return-value]


def check_all_porting_docs() -> None:
    apps = declared_apps()
    actual_dirs = {p.name for p in (ROOT / "apps").iterdir() if p.is_dir()}
    declared = set(apps)
    extra_dirs = actual_dirs - declared
    missing_dirs = declared - actual_dirs
    require(actual_dirs == declared,
            f"apps directory/governance mismatch extra_dirs={sorted(extra_dirs)} missing_dirs={sorted(missing_dirs)}")
    for app_name in sorted(declared):
        porting_path = ROOT / "apps" / app_name / "PORTING.md"
        require(porting_path.exists(), f"apps/{app_name}: missing PORTING.md")
        text = read(porting_path)
        rel = porting_path.relative_to(ROOT)
        for section in REQUIRED_PORTING_SECTIONS:
            require(section in text, f"{rel}: missing {section}")
        require("make " in text, f"{rel}: missing runnable make verification command")
        require("Forbidden" in text or "forbidden" in text, f"{rel}: missing forbidden-claim boundary")


def check_readme_app_statuses() -> None:
    apps = declared_apps()
    readme = read(ROOT / "README.md")
    for app_name, meta in sorted(apps.items()):
        if not isinstance(meta, dict):
            continue
        status = str(meta.get("status", ""))
        require(f"| `{app_name}` | {status} |" in readme,
                f"README.md app table must list {app_name} with governance status {status}")


def main() -> int:
    try:
        for name, meta in APPS.items():
            check_app(name, meta)
        check_all_porting_docs()
        check_readme_app_statuses()
        read(ROOT / "README.md")
        paper = "\n".join(read(p) for p in [
            ROOT / "paper" / "sections" / "06-graphics-runtime-application-support.typ",
            ROOT / "paper" / "sections" / "07-implementation.typ",
            ROOT / "paper" / "sections" / "08-evaluation.typ",
        ])
        require("official-source" in paper or "official upstream" in paper,
                "paper sections must describe official-source porting")
    except AssertionError as exc:
        print(f"app_port_check: FAIL {exc}")
        return 1
    print("app_port_check: PASS official-source Unikraft app port metadata")
    return 0


if __name__ == "__main__":
    sys.exit(main())

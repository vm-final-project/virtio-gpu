#!/usr/bin/env python3
"""Validate Unikraft-style README files for project local libraries."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIBS = sorted(p for p in (ROOT / "libs").iterdir() if p.is_dir())
REQUIRED_COMMON = [
    "# ",
    "## Configuring applications to use",
    "## Public",
    "## Design boundaries",
    "## Verification",
    "CONFIG_",
    "make verify",
]
BOUNDARY_TERMS = ["not", "must not", "does not", "blocked", "boundary"]


def check_lib(path: Path) -> list[str]:
    errors: list[str] = []
    readme = path / "README.md"
    config = path / "Config.uk"
    makefile = path / "Makefile.uk"
    if not readme.exists():
        return [f"{path.relative_to(ROOT)}: missing README.md"]
    text = readme.read_text(errors="replace")
    cfg_text = config.read_text(errors="replace") if config.exists() else ""
    mk_text = makefile.read_text(errors="replace") if makefile.exists() else ""
    libname = path.name
    if libname not in text:
        errors.append(f"{path.relative_to(ROOT)}: README title/body must mention {libname}")
    for needle in REQUIRED_COMMON:
        if needle not in text:
            errors.append(f"{path.relative_to(ROOT)}: README missing {needle!r}")
    # Require the concrete Kconfig symbol exposed by Config.uk.
    symbols = [line.split()[1] for line in cfg_text.splitlines() if line.startswith("config ")]
    if symbols and not any(sym in text for sym in symbols):
        errors.append(f"{path.relative_to(ROOT)}: README missing Config.uk symbol from {symbols}")
    if "addlib" in mk_text and "addlib" not in text and "registers the library" not in text:
        errors.append(f"{path.relative_to(ROOT)}: README should mention Unikraft library registration/use")
    if not any(term in text for term in BOUNDARY_TERMS):
        errors.append(f"{path.relative_to(ROOT)}: README needs an explicit limitation/boundary")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    errors: list[str] = []
    for lib in LIBS:
        errors.extend(check_lib(lib))
    if errors:
        print("lib_readme_check: FAIL", file=sys.stderr)
        for err in errors:
            print(f"- {err}", file=sys.stderr)
        return 1
    print(f"lib_readme_check: PASS libs={len(LIBS)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

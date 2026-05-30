#!/usr/bin/env python3
"""Reject legacy cryptic row IDs outside the authoritative map and audit history.

Run: `make naming-check`.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAP_FILE = ROOT / "config" / "row_id_map.json"

ALLOWED_DIRS = (
    ".omx", ".git", ".unikraft", ".tools",
    "results/.archive", "paper/clean-acmart", "paper/figures",
    "resource", "rootfs", "tests/build",
    "omx_wiki", "design",
)
ALLOWED_FILES = {
    "config/row_id_map.json",
    "scripts/naming_check.py",
}

# Hard-coded legacy tokens, assembled from character fragments so the file is
# tolerant to accidental bulk sweeps (a renamer that walks `_LEGACY_BITS`
# entries finds neither old nor new namespace tokens to rewrite).
def _t(*parts: str) -> str:
    return "".join(parts)

_LEGACY_TOKENS_LITERAL = [
    _t("N", "2D"),
    _t("G", "0"),
    _t("K1", "sw"), _t("K1", "-submit"), _t("K1", "-frame"), _t("G1", "sw"),
    _t("U2", "GL"),
    _t("VA", "BI"), _t("VQ", "EMU"), _t("VP", "ERF"),
    _t("VENUS", "-ENC"), _t("VENUS", "-RING"),
    _t("VSM", "OKE"), _t("VKM", "ARK"),
    _t("LLAMA-VK-", "LINUX"), _t("LLAMA-VK-", "PROBE"), _t("LLAMA-VK-", "BUILD"),
    _t("LLAMA-VK-", "RUN"), _t("LLAMA-VK-", "BENCH"),
    _t("LLAMA-VK-N3-", "DISPATCH"), _t("LLAMA-VK-N3-", "BUILD-PASS"),
    _t("LLAMA-VK-N3-", "BUILD"), _t("LLAMA-VK-N3-", "RUN"),
    _t("LLAMA-UPSTREAM-", "CPU"), _t("LLAMA-UPSTREAM-", "VK"),
    _t("LLAMA-UPSTREAM-", "SERVER"), _t("ENV10-", "REAL"),
    _t("LLAMA", "0"), _t("LLAMA", "1"), _t("LLAMA", "2"),
    _t("LLAMA-", "CPU-RUN"), _t("LLAMA-VK-", "UK"), _t("LLAMA-VK-", "CPP"),
]
LEGACY_TOKEN_RE = re.compile(
    r"(?<![A-Za-z0-9_.])("
    + "|".join(re.escape(t) for t in _LEGACY_TOKENS_LITERAL)
    + r")(?![A-Za-z0-9_.])"
)

SCAN_GLOBS = ("*.md", "*.typ", "*.py", "*.uk", "*.json", "*.yaml", "*.yml", "Makefile", "Kraftfile*")


def _is_allowed(path: Path) -> bool:
    rel = path.relative_to(ROOT).as_posix()
    if rel in ALLOWED_FILES:
        return True
    for prefix in ALLOWED_DIRS:
        if rel == prefix or rel.startswith(prefix + "/"):
            return True
    return False


def iter_files() -> list[Path]:
    out: list[Path] = []
    for glob in SCAN_GLOBS:
        for path in ROOT.rglob(glob):
            if path.is_file() and not _is_allowed(path):
                out.append(path)
    return sorted(set(out))


def main() -> int:
    if not MAP_FILE.exists():
        print(f"naming_check: FAIL missing {MAP_FILE.relative_to(ROOT)}")
        return 1
    failures: list[str] = []
    files = iter_files()
    for path in files:
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        rel = path.relative_to(ROOT).as_posix()
        for m in LEGACY_TOKEN_RE.finditer(text):
            ln = text.count("\n", 0, m.start()) + 1
            failures.append(f"{rel}:{ln} legacy token `{m.group(1)}`")
    if failures:
        print("naming_check: FAIL")
        for f in failures[:50]:
            print(f"  {f}")
        if len(failures) > 50:
            print(f"  ... and {len(failures) - 50} more")
        print(f"see {MAP_FILE.relative_to(ROOT)} for the new namespace")
        return 1
    print(f"naming_check: PASS scanned={len(files)} legacy_tokens={len(_LEGACY_TOKENS_LITERAL)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

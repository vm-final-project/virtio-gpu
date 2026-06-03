#!/usr/bin/env python3
"""Generator scaffold for libs/libukvenus, modeled on venus-protocol.

`venus-protocol` (in this repository's sibling tree) walks `xmls/vk.xml` plus
`VK_MESA_venus_protocol.xml` / `VK_EXT_command_serialization.xml`, then renders
Mako templates into `include/venus_protocol/*.h`.  libukvenus already follows
the same wire format on the guest side, so the same flow can autogenerate the
project-local encoder.

This file is intentionally small: it defines the artifact layout and a
machine-readable plan, and shells out to the upstream `venus-protocol` checkout
when present so we never fork the templates.  The implementation work the user
needs to do to fully replace hand-written encoder code is:

  1. Pin a `venus-protocol` upstream commit (sibling checkout
     `../../venus-protocol`).
  2. Symlink/copy the upstream `xmls/` into `scripts/venus/xmls/` so the
     generator can be re-run offline.
  3. Add Mako templates under `scripts/venus/templates/` that emit:
         libs/libukvenus/include/uk/venus_protocol.h
         libs/libukvenus/include/uk/venus_dispatch.h
         libs/libukvenus/generated/venus_cs_encode.c
     Only Venus-encoder slices we actually use (instance/device/command-buffer/
     pipeline) should be enabled in `VK_XML_EXTENSION_LIST` to keep the
     resulting image minimal.
  4. Wire `make gen-libukvenus` into the build so a regen is a single command.

Run modes:

    python3 scripts/gen_libukvenus.py --plan       # print artifact plan as JSON
    python3 scripts/gen_libukvenus.py --check      # validate venus-protocol checkout
    python3 scripts/gen_libukvenus.py --generate   # invoke upstream generator

The actual generator is delegated to the sibling `venus-protocol/vn_protocol.py`
to avoid forking 3,000 lines of Mesa code into VOGUE.
"""
from __future__ import annotations

import argparse
import filecmp
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIBVENUS = ROOT / "libs" / "libukvenus"
OUTDIR = LIBVENUS / "generated"

# The pinned upstream generator commit + slice config is the single source of
# truth. See scripts/venus/pin.json and scripts/venus/test_pin.py.
PIN = json.loads((ROOT / "scripts/venus/pin.json").read_text())
VENUS_PROTOCOL = (ROOT / PIN["checkout"]).resolve()

def cmd_plan(_args: argparse.Namespace) -> int:
    """Print the real generation plan, sourced from pin.json + the committed tree."""
    generated = sorted(p.name for p in OUTDIR.glob("vn_protocol_driver_*.h"))
    payload = {
        "generator": str(VENUS_PROTOCOL / PIN["generator"]),
        "checkout": str(VENUS_PROTOCOL),
        "commit": PIN["commit"],
        "variant": PIN["variant"],
        "outdir": str(OUTDIR.relative_to(ROOT)),
        "generated_header_count": len(generated),
        "lock": str((OUTDIR / "GENERATED.lock").relative_to(ROOT)),
        "make_targets": {
            "generate": "gen-libukvenus",
            "verify": "gen-libukvenus-verify",
            "selftest": "gen-libukvenus-selftest",
        },
        "notes": [
            "Driver headers are committed verbatim under libs/libukvenus/generated/ "
            "with a sha256 GENERATED.lock; gen-libukvenus-verify gates governance-check.",
            "The in-image encoders in libs/libukvenus/{venus_cs,venus_compute}.c are "
            "byte-for-byte parity-locked to the generated reference (make -C tests "
            "venus-parity); they are not regenerated into the image. See GENERATOR.md.",
        ],
    }
    print(json.dumps(payload, indent=2))
    return 0


def cmd_check(_args: argparse.Namespace) -> int:
    problems: list[str] = []
    if not VENUS_PROTOCOL.is_dir():
        problems.append(f"missing sibling checkout: {VENUS_PROTOCOL}")
    else:
        for path in ("vn_protocol.py", "vkxml.py", "xmls/vk.xml", "templates"):
            if not (VENUS_PROTOCOL / path).exists():
                problems.append(f"venus-protocol/{path} missing")
    if shutil.which("python3") is None:
        problems.append("python3 not on PATH")
    try:
        import mako  # noqa: F401
    except ImportError:
        problems.append("python module 'mako' not installed (pip install Mako)")

    if problems:
        for p in problems:
            print(f"gen_libukvenus check: FAIL {p}")
        return 1
    print(f"gen_libukvenus check: PASS venus-protocol={VENUS_PROTOCOL}")
    return 0


def _run_upstream(dest: Path) -> None:
    """Run the pinned upstream generator into dest (driver/guest variant)."""
    dest.mkdir(parents=True, exist_ok=True)
    for stale in dest.glob("vn_protocol_driver_*.h"):
        stale.unlink()
    cmd = [sys.executable, str(VENUS_PROTOCOL / PIN["generator"]),
           "--outdir", str(dest)]  # no --renderer => driver/guest encoder variant
    subprocess.run(cmd, check=True, cwd=str(VENUS_PROTOCOL))


def _write_lock(dest: Path) -> None:
    files = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(dest.glob("vn_protocol_driver_*.h"))}
    lock = {"commit": PIN["commit"], "variant": PIN["variant"], "files": files}
    (dest / "GENERATED.lock").write_text(json.dumps(lock, indent=2) + "\n")


def cmd_generate(args: argparse.Namespace) -> int:
    if cmd_check(args) != 0:
        return 1
    _run_upstream(OUTDIR)
    _write_lock(OUTDIR)
    print(f"gen_libukvenus: PASS wrote {OUTDIR.relative_to(ROOT)}")
    return 0


def cmd_verify(args: argparse.Namespace) -> int:
    """Regenerate into a temp dir and diff against the committed tree."""
    if cmd_check(args) != 0:
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        _run_upstream(tmp)
        _write_lock(tmp)
        names = {p.name for p in OUTDIR.glob("vn_protocol_driver_*.h")}
        names |= {p.name for p in tmp.glob("vn_protocol_driver_*.h")}
        diffs = [n for n in sorted(names)
                 if not (OUTDIR / n).exists() or not (tmp / n).exists()
                 or not filecmp.cmp(OUTDIR / n, tmp / n, shallow=False)]
        if diffs:
            print("gen_libukvenus verify: FAIL drift in: " + ", ".join(diffs))
            return 1
    print("gen_libukvenus verify: PASS committed tree matches upstream regen")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="mode", required=False)
    sub.add_parser("plan").set_defaults(func=cmd_plan)
    sub.add_parser("check").set_defaults(func=cmd_check)
    sub.add_parser("generate").set_defaults(func=cmd_generate)
    sub.add_parser("verify").set_defaults(func=cmd_verify)
    parser.add_argument("--plan", dest="legacy_plan", action="store_true")
    parser.add_argument("--check", dest="legacy_check", action="store_true")
    parser.add_argument("--generate", dest="legacy_generate", action="store_true")

    args = parser.parse_args(argv)
    if getattr(args, "legacy_plan", False):
        return cmd_plan(args)
    if getattr(args, "legacy_check", False):
        return cmd_check(args)
    if getattr(args, "legacy_generate", False):
        return cmd_generate(args)
    if getattr(args, "func", None) is not None:
        return args.func(args)
    parser.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())

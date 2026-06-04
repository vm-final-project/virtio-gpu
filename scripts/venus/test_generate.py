import hashlib
import json
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _runner import run  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
GEN = ROOT / "scripts/gen_libukvenus.py"
OUTDIR = ROOT / "libs/libukvulkan_venus/generated"


def _gen():
    subprocess.check_call([sys.executable, str(GEN), "generate"])


def _hashes():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in OUTDIR.glob("vn_protocol_driver_*.h")}


def test_generate_is_deterministic():
    _gen()
    first = _hashes()
    _gen()
    second = _hashes()
    assert first == second and first, "generation not reproducible"


def test_lock_matches_output():
    _gen()
    lock = json.loads((OUTDIR / "GENERATED.lock").read_text())
    for name, sha in lock["files"].items():
        assert hashlib.sha256((OUTDIR / name).read_bytes()).hexdigest() == sha


def test_encoder_present_and_no_external_deps():
    _gen()
    cmdbuf = (OUTDIR / "vn_protocol_driver_command_buffer.h").read_text()
    assert "vn_encode_vkCmdDispatch" in cmdbuf
    incs = set(re.findall(r'#include\s+["<]([^">]+)[">]',
               "\n".join(p.read_text() for p in OUTDIR.glob("*.h"))))
    allowed = ("vn_protocol_driver_", "vn_cs.h", "vn_ring.h",
               "vulkan/", "vk_video/", "vk_platform.h",
               "string.h", "stdlib.h", "assert.h")
    bad = [i for i in incs if not i.startswith(allowed)]
    assert not bad, f"unexpected external include(s): {bad}"


def test_verify_passes_on_committed_tree():
    subprocess.check_call([sys.executable, str(GEN), "verify"])


if __name__ == "__main__":
    run(globals())

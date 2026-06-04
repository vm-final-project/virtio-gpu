import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _runner import run  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUTDIR = ROOT / "libs/libukvulkan_venus/generated"
MANIFEST = json.loads((ROOT / "config/venus_command_manifest.json").read_text())


def test_every_ggml_command_has_an_encoder():
    blob = "\n".join(p.read_text() for p in OUTDIR.glob("vn_protocol_driver_*.h"))
    have = set(re.findall(r"vn_encode_(vk[A-Za-z0-9]+)\b", blob))
    missing = [c for c in MANIFEST["commands"] if c not in have]
    assert not missing, f"generated set lacks encoders for: {missing}"


if __name__ == "__main__":
    run(globals())

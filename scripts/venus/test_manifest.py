import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _runner import run  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "config/venus_command_manifest.json"
SCRIPT = ROOT / "scripts/extract_ggml_vk_commands.py"


def test_manifest_matches_live_scan():
    live = json.loads(subprocess.check_output(
        [sys.executable, str(SCRIPT), "--json"], text=True))
    frozen = json.loads(MANIFEST.read_text())
    assert frozen["commands"] == live["commands"], \
        "manifest drifted from ggml-vulkan.cpp; regenerate it"
    assert frozen["client_side"] == live["client_side"], \
        "client_side drifted from ggml-vulkan.cpp; regenerate it"


if __name__ == "__main__":
    run(globals())

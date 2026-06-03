import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _runner import run  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/extract_ggml_vk_commands.py"


def _run():
    out = subprocess.check_output([sys.executable, str(SCRIPT), "--json"], text=True)
    return json.loads(out)


def test_contains_core_compute_commands():
    cmds = set(_run()["commands"])
    for must in ["vkCreateBuffer", "vkAllocateMemory", "vkCmdDispatch",
                 "vkCreateComputePipelines", "vkQueueSubmit",
                 "vkUpdateDescriptorSets", "vkCmdPushConstants"]:
        assert must in cmds, f"missing {must}"


def test_client_side_commands_excluded_from_wire_set():
    data = _run()
    cmds = set(data["commands"])
    client = set(data["client_side"])
    # loader + blob-mapped memory are resolved guest-side, never serialized
    assert "vkGetInstanceProcAddr" in client
    assert "vkMapMemory" in client
    assert not (cmds & client), "client-side command leaked into wire set"


def test_output_is_sorted_and_deterministic():
    a = _run()["commands"]
    b = _run()["commands"]
    assert a == b, "non-deterministic output"
    assert a == sorted(a), "output not sorted"


if __name__ == "__main__":
    run(globals())

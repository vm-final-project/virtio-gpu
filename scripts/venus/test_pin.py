import json
import pathlib
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from _runner import run  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
PIN = ROOT / "scripts/venus/pin.json"


def test_pin_matches_checkout():
    pin = json.loads(PIN.read_text())
    checkout = (ROOT / pin["checkout"]).resolve()
    head = subprocess.check_output(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    assert head == pin["commit"], f"venus-protocol moved: {head} != {pin['commit']}"


def test_pin_has_slice_fields():
    pin = json.loads(PIN.read_text())
    assert pin["variant"] == "driver"
    assert isinstance(pin["wanted_extensions"], list) and pin["wanted_extensions"]


if __name__ == "__main__":
    run(globals())

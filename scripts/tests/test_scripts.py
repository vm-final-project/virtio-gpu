from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

import common


def _load(name: str, filename: str):
    """Import a runner module whose filename is not a valid identifier."""
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# The runner files use hyphens (app-llama-*.py), so load them by path.
llama_cpu = _load("llama_cpu", "app-llama-cpu.py")
llama_vk = _load("llama_vk", "app-llama-vk.py")


class CommonTests(unittest.TestCase):
    def test_result_has_small_canonical_schema(self) -> None:
        result = common.result(
            "pass",
            ["qemu", "-kernel", "image"],
            inputs={"model": "model.gguf"},
            metrics={"tg128": 1.5},
        )
        self.assertEqual(
            set(result),
            {"status", "generated_utc", "command", "inputs", "metrics", "error"},
        )

    def test_resolve_model_requires_existing_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / "model.gguf"
            self.assertIsNone(common.resolve_model(model))
            model.write_bytes(b"gguf")
            self.assertEqual(common.resolve_model(model), model)

    def test_acceleration_falls_back_to_tcg(self) -> None:
        self.assertEqual(common.acceleration("x86_64", Path("/missing/kvm")), "tcg")

    def test_arch_helpers(self) -> None:
        self.assertEqual(common.normalize_arch("amd64"), "x86_64")
        self.assertEqual(common.normalize_arch("aarch64"), "arm64")
        self.assertEqual(common.default_console("arm64"), "ttyAMA0")
        self.assertEqual(common.image_suffix("arm64"), "qemu-arm64")

    def test_run_timed_times_serial_markers_in_order(self) -> None:
        script = (
            "import time\n"
            "print('starting', flush=True)\n"
            "time.sleep(0.12)\n"
            "print('guest BOOTED now', flush=True)\n"
            "time.sleep(0.12)\n"
            "print('running llama-bench', flush=True)\n"
        )
        run = common.run_timed(
            [sys.executable, "-c", script], timeout=30,
            markers={"boot": "guest booted", "ready": "running llama-bench"},
        )
        self.assertFalse(run.timed_out)
        self.assertEqual(run.returncode, 0)
        self.assertIn("starting", run.text)
        # Matching is case-insensitive and timestamps follow emission order.
        self.assertIsNotNone(run.elapsed("boot"))
        self.assertGreater(run.elapsed("ready"), run.elapsed("boot"))
        self.assertIsNone(run.elapsed("never-printed"))

    def test_run_timed_kills_on_timeout_but_keeps_markers(self) -> None:
        run = common.run_timed(
            [sys.executable, "-c", "import time; print('up', flush=True); time.sleep(30)"],
            timeout=0.5, markers={"boot": "up"},
        )
        self.assertTrue(run.timed_out)
        self.assertIsNotNone(run.elapsed("boot"))

    def test_file_size(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "image.bin"
            self.assertIsNone(common.file_size(path))   # missing
            self.assertIsNone(common.file_size(None))
            self.assertIsNone(common.file_size(directory))  # a dir, not a file
            path.write_bytes(b"x" * 4096)
            self.assertEqual(common.file_size(path), 4096)

    def test_run_timed_captures_peak_rss(self) -> None:
        # The child allocates ~20 MiB; its peak RSS is reported via getrusage.
        run = common.run_timed(
            [sys.executable, "-c", "a = bytearray(20 * 1024 * 1024); print('ok')"],
            timeout=30,
        )
        self.assertEqual(run.returncode, 0)
        self.assertIsInstance(run.peak_rss_kb, int)
        self.assertGreater(run.peak_rss_kb, 1024)  # well above 1 MiB


# A stand-in for a server appliance: one process that prints the ready marker
# on its serial log (stdout), then serves /health and an OpenAI-compatible
# /v1/chat/completions that echoes the received user message back inside the
# reply. This mirrors reality, where the same process emits READY and serves
# HTTP, so a healthy /health implies the marker was already printed.
_FAKE_SERVER = """
import http.server, json, sys
PORT = int(sys.argv[1])
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.end_headers(); self.wfile.write(b'{"status":"ok"}')
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        msgs = json.loads(self.rfile.read(n) or b"{}").get("messages", [{}])
        prompt = msgs[-1].get("content", "")
        out = json.dumps({
            "choices": [{"message": {"role": "assistant", "content": "reply to: " + prompt}}],
            "usage": {"completion_tokens": 5},
        }).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json")
        self.end_headers(); self.wfile.write(out)
srv = http.server.HTTPServer(("127.0.0.1", PORT), H)
print("uk-test-server: READY", flush=True)
srv.serve_forever()
"""


class HttpProbeTests(unittest.TestCase):
    def test_probe_records_query_and_completion(self) -> None:
        port = common.free_port()
        command = [sys.executable, "-c", _FAKE_SERVER, str(port)]
        metrics, log, passed = common.probe_http_server(
            command, port=port, ready_marker="uk-test-server: READY",
            query="hi, what's your name", timeout=10,
        )

        self.assertTrue(passed)
        self.assertIn("uk-test-server: READY", log)
        # The real query reached the server and the round-trip was recorded:
        # query verbatim, and the completion the server produced from it.
        self.assertEqual(metrics["query"], "hi, what's your name")
        self.assertEqual(metrics["completion"], "reply to: hi, what's your name")
        self.assertEqual(metrics["http_status"], 200)
        self.assertEqual(metrics["completion_status"], 200)
        self.assertTrue(metrics["ready"])


class CommandTests(unittest.TestCase):
    def test_cpu_modes_select_distinct_images(self) -> None:
        bench = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, 18080, "x86_64")
        server = llama_cpu.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64")
        self.assertIn("vogue-llama-cpu_qemu-x86_64", " ".join(bench))
        self.assertIn("vogue-llama-cpu-server_qemu-x86_64", " ".join(server))
        # Only the server appliance gets a NIC + hostfwd to its HTTP listener.
        self.assertNotIn("hostfwd", " ".join(bench))
        self.assertIn("hostfwd", " ".join(server))

    def test_cpu_arm64_uses_arm_console_and_image(self) -> None:
        bench = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, 18080, "arm64")
        self.assertIn("vogue-llama-cpu_qemu-arm64", " ".join(bench))
        self.assertIn("virt,accel=", " ".join(bench))

    def test_vk_modes_select_distinct_images(self) -> None:
        bench = llama_vk.qemu_command("qemu", Path("model"), "bench", 10, 18080, "x86_64")
        server = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64")
        self.assertIn("vogue-llama-vk_qemu-x86_64", " ".join(bench))
        self.assertIn("vogue-llama-vk-server_qemu-x86_64", " ".join(server))
        self.assertNotIn("hostfwd", " ".join(bench))
        self.assertIn("hostfwd", " ".join(server))

    def test_result_path_keeps_x86_and_suffixes_arm64(self) -> None:
        root = Path("/tmp/results")
        self.assertEqual(common.result_path(root, "llama_cpu.json", "x86_64"), root / "llama_cpu.json")
        self.assertEqual(common.result_path(root, "llama_cpu.json", "arm64"), root / "llama_cpu_arm64.json")


if __name__ == "__main__":
    unittest.main()

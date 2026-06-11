from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import common
import smp_topology
import importlib.util


def _load(alias: str, filename: str):
    """Load a hyphen-named sibling script under an importable alias.

    The runtime scripts use hyphenated filenames that match their make targets
    (e.g. ``app-llama-vk.py``), which cannot be imported with a plain ``import``
    statement. Load them by path instead.
    """
    path = Path(__file__).resolve().parents[1] / filename
    spec = importlib.util.spec_from_file_location(alias, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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

    def test_smp_args_omitted_for_single_vcpu(self) -> None:
        self.assertEqual(common.smp_args(1), [])
        self.assertEqual(common.smp_args(0), [])

    def test_smp_args_emits_flag_for_multiple_vcpus(self) -> None:
        self.assertEqual(common.smp_args(4), ["-smp", "4"])


class CommandTests(unittest.TestCase):
    def test_cpu_modes_select_distinct_images(self) -> None:
        bench = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "x86_64")
        server = llama_cpu.qemu_command("qemu", Path("model"), "server", 10, "x86_64")
        self.assertIn("vogue-llama-cpu_qemu-x86_64", " ".join(bench))
        self.assertIn("vogue-llama-cpu-server_qemu-x86_64", " ".join(server))

    def test_cpu_arm64_uses_arm_console_and_image(self) -> None:
        bench = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "arm64")
        self.assertIn("vogue-llama-cpu_qemu-arm64", " ".join(bench))
        self.assertIn("virt,accel=", " ".join(bench))

    def test_vk_modes_select_distinct_images(self) -> None:
        bench = llama_vk.qemu_command("qemu", Path("model"), "bench", 10, 18080, "x86_64")
        server = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64")
        self.assertIn("vogue-llama-vk_qemu-x86_64", " ".join(bench))
        self.assertIn("vogue-llama-vk-server_qemu-x86_64", " ".join(server))
        self.assertNotIn("hostfwd", " ".join(bench))
        self.assertIn("hostfwd", " ".join(server))

    def test_vk_smp_flag_present_only_when_multivcpu(self) -> None:
        one = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64", smp=1)
        four = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64", smp=4)
        self.assertNotIn("-smp", one)
        self.assertEqual(four[four.index("-smp") + 1], "4")

    def test_cpu_smp_flag_present_only_when_multivcpu(self) -> None:
        one = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "x86_64", smp=1)
        four = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "x86_64", smp=4)
        self.assertNotIn("-smp", one)
        self.assertEqual(four[four.index("-smp") + 1], "4")

    def test_result_path_keeps_x86_and_suffixes_arm64(self) -> None:
        root = Path("/tmp/results")
        self.assertEqual(common.result_path(root, "llama_cpu.json", "x86_64"), root / "llama_cpu.json")
        self.assertEqual(common.result_path(root, "llama_cpu.json", "arm64"), root / "llama_cpu_arm64.json")


class SmpTopologyTests(unittest.TestCase):
    def test_round_robin_pins_one_worker_per_lcpu(self) -> None:
        # 4 vCPUs, 4 ggml workers -> one each, distinct LCPUs
        self.assertEqual([smp_topology.place(i, 4) for i in range(4)], [0, 1, 2, 3])

    def test_wraps_when_more_threads_than_lcpus(self) -> None:
        self.assertEqual([smp_topology.place(i, 4) for i in range(6)], [0, 1, 2, 3, 0, 1])

    def test_single_lcpu_pins_all_to_zero(self) -> None:
        self.assertEqual([smp_topology.place(i, 1) for i in range(3)], [0, 0, 0])


if __name__ == "__main__":
    unittest.main()

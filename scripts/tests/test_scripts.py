from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
import sys
import re

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


def _load_pthread_affinity():
    path = Path(__file__).resolve().parents[1] / "app-pthread-affinity.py"
    if not path.is_file():
        return None
    return _load("pthread_affinity", "app-pthread-affinity.py")


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

    def test_cpu_server_qemu_command_exposes_hostfwd_networking(self) -> None:
        server = llama_cpu.qemu_command("qemu", Path("model"), "server", 10, "x86_64", port=18080)
        joined = " ".join(server)
        self.assertIn("hostfwd=tcp:127.0.0.1:18080-10.0.2.15:8080", joined)
        self.assertIn("virtio-net-pci", joined)

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

    def test_llama_cpu_runner_collects_unikraft_placement_metrics(self) -> None:
        log = "\n".join(
            [
                "[    5.109477] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x1 on-lcpu=1 sched-lcpu=1 ready=1",
                "[    5.122071] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x2 on-lcpu=2 sched-lcpu=2 ready=1",
                "uk-llama-upstream: pp512=100.0 tg128=20.0",
            ]
        )
        match = re.search(r"uk-llama-upstream: pp512=([0-9.]+) tg128=([0-9.]+)", log)
        placements = [
            {"target": int(target), "actual": int(actual)}
            for target, actual in re.findall(
                r"ggml-unikraft-place: target=(\d+) actual=(-?\d+)",
                log,
            )
        ]
        kernel_placements = [
            {"actual": int(actual), "target": int(target)}
            for actual, target in re.findall(
                r"SMPPLACE dequeue thread=.* on-lcpu=(\d+) sched-lcpu=(\d+) ready=1",
                log,
            )
        ]

        self.assertIsNotNone(match)
        self.assertEqual(placements, [])
        self.assertEqual(
            kernel_placements,
            [
                {"actual": 1, "target": 1},
                {"actual": 2, "target": 2},
            ],
        )

    def test_llama_server_metrics_can_include_ready_tps_and_kernel_placements(self) -> None:
        log = "\n".join(
            [
                "uk-llama-upstream-server: READY model=/mnt/model/model.gguf threads=4 slots=1 prompt_cache=1 mode=single-app no_fork_exec=1",
                "[    5.109477] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x1 on-lcpu=1 sched-lcpu=1 ready=1",
                "[    5.122071] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x2 on-lcpu=2 sched-lcpu=2 ready=1",
            ]
        )
        ready = re.search(r"uk-llama-upstream-server: READY ([^\n]+)", log)
        kernel_placements = [
            {"actual": int(actual), "target": int(target)}
            for actual, target in re.findall(
                r"SMPPLACE dequeue thread=.* on-lcpu=(\d+) sched-lcpu=(\d+) ready=1",
                log,
            )
        ]
        metrics = {"ready": ready.group(0).strip(), "server_toks_per_s": 42.5}
        if kernel_placements:
            metrics["kernel_placements"] = kernel_placements

        self.assertEqual(
            metrics,
            {
                "ready": "uk-llama-upstream-server: READY model=/mnt/model/model.gguf threads=4 slots=1 prompt_cache=1 mode=single-app no_fork_exec=1",
                "server_toks_per_s": 42.5,
                "kernel_placements": [
                    {"actual": 1, "target": 1},
                    {"actual": 2, "target": 2},
                ],
            },
        )


class SmpTopologyTests(unittest.TestCase):
    def test_effective_mask_intersects_requested_and_online(self) -> None:
        self.assertEqual(
            smp_topology.effective_mask(0b1110, 0b0011),
            0b0010,
        )

    def test_empty_effective_mask_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            smp_topology.effective_mask(0b1000, 0b0011)

    def test_keep_current_owner_when_allowed(self) -> None:
        self.assertEqual(
            smp_topology.choose_target(0b1010, current=3),
            3,
        )

    def test_choose_lowest_allowed_when_current_disallowed(self) -> None:
        self.assertEqual(
            smp_topology.choose_target(0b1010, current=0),
            1,
        )

    def test_reject_empty_mask_when_choosing_target(self) -> None:
        with self.assertRaises(ValueError):
            smp_topology.choose_target(0, current=0)

    def test_worker_masks_are_one_hot(self) -> None:
        self.assertEqual(
            smp_topology.worker_masks(4, 4),
            [0b0001, 0b0010, 0b0100, 0b1000],
        )

    def test_reject_more_workers_than_lcpus(self) -> None:
        with self.assertRaises(ValueError):
            smp_topology.worker_masks(5, 4)

    def test_child_worker_masks_skip_cpu0_for_parent(self) -> None:
        self.assertEqual(
            smp_topology.child_worker_masks(4, 4),
            [0b0010, 0b0100, 0b1000],
        )


class UnikraftAffinitySourceTests(unittest.TestCase):
    def test_sched_affinity_syscalls_are_not_stubbed(self) -> None:
        sched_c = (
            Path(__file__).resolve().parents[2]
            / ".deps/src/unikraft/lib/uksched/sched.c"
        ).read_text()
        self.assertIn("UK_SYSCALL_R_DEFINE(int, sched_getaffinity", sched_c)
        self.assertTrue(
            "UK_SYSCALL_R_DEFINE(int, sched_setaffinity" in sched_c
            or "UK_SYSCALL_R_E_DEFINE(int, sched_setaffinity" in sched_c
        )
        self.assertNotIn("UK_WARN_STUBBED();", sched_c)

    def test_posix_process_exports_tid_lookup_without_posix_thread_type(self) -> None:
        process_h = (
            Path(__file__).resolve().parents[2]
            / ".deps/src/unikraft/lib/posix-process/include/uk/process.h"
        ).read_text()
        self.assertIn("struct uk_thread *uk_posix_thread_from_tid(int tid);", process_h)
        self.assertNotIn("struct posix_thread", process_h)

    def test_clone_path_no_longer_uses_global_round_robin_pick(self) -> None:
        clone_c = (
            Path(__file__).resolve().parents[2]
            / ".deps/src/unikraft/lib/posix-process/clone.c"
        ).read_text()
        self.assertIn("s = uk_sched_current();", clone_c)
        self.assertNotIn("s = uk_schedcoop_smp_pick();", clone_c)

    def test_schedcoop_exports_distinct_current_thread_placement_helper(self) -> None:
        root = Path(__file__).resolve().parents[2] / ".deps/src/unikraft/lib/ukschedcoop"
        public_h = (root / "include/uk/schedcoop.h").read_text()
        exportsyms = (root / "exportsyms.uk").read_text()
        smp_c = (root / "smp.c").read_text()

        self.assertIn(
            "int uk_schedcoop_smp_place_current(unsigned int target_lcpu);",
            public_h,
        )
        self.assertIn("uk_schedcoop_smp_place_current", exportsyms)
        self.assertIn(
            "int uk_schedcoop_smp_place_current(unsigned int target_lcpu)",
            smp_c,
        )
        self.assertIn("thread->affinity_target_lcpu = target_lcpu;", smp_c)
        self.assertIn("thread->affinity_migrate_pending =", smp_c)

    def test_ggml_unikraft_worker_entry_uses_narrow_place_current_helper(self) -> None:
        ggml_cpu_c = (
            Path(__file__).resolve().parents[2]
            / ".deps/src/llama.cpp/ggml/src/ggml-cpu/ggml-cpu.c"
        ).read_text()
        self.assertIn("#if defined(__Unikraft__)", ggml_cpu_c)
        self.assertIn("#include <uk/schedcoop.h>", ggml_cpu_c)
        self.assertIn("uk_schedcoop_smp_place_current(target_lcpu);", ggml_cpu_c)

    def test_cpu_kraftfiles_parameterize_worker_count_from_vogue_smp(self) -> None:
        root = Path(__file__).resolve().parents[2] / "kraft"
        bench = (root / "Kraftfile.llama-cpu-bench").read_text()
        server = (root / "Kraftfile.llama-cpu-server").read_text()

        self.assertIn("CONFIG_APP_LLAMA_CPU_THREADS: '@@VOGUE_SMP@@'", bench)
        self.assertIn("CONFIG_UKPLAT_CPU_MAXCOUNT: '4'", bench)
        self.assertIn("CONFIG_APP_LLAMA_CPU_THREADS: '@@VOGUE_SMP@@'", server)
        self.assertIn("CONFIG_UKPLAT_CPU_MAXCOUNT: '4'", server)

    def test_cpu_worker_thread_count_uses_configured_app_thread_count(self) -> None:
        common_h = (
            Path(__file__).resolve().parents[2]
            / "apps/app-llama-cpu/llama-cpu-common.h"
        ).read_text()
        self.assertIn(
            "#define UK_LLAMA_CPU_WORKER_THREADS CONFIG_APP_LLAMA_CPU_THREADS",
            common_h,
        )
        self.assertNotIn(
            "#define UK_LLAMA_CPU_WORKER_THREADS CONFIG_UKPLAT_CPU_MAXCOUNT",
            common_h,
        )


class PthreadAffinityRunnerTests(unittest.TestCase):
    def test_parse_success_log_extracts_workers(self) -> None:
        module = _load_pthread_affinity()
        self.assertIsNotNone(module, "scripts/app-pthread-affinity.py must exist")
        parsed = module.parse_probe_log(
            "\n".join(
                [
                    "pthread-affinity: worker=0 requested=0x1 actual=0 samples=100000",
                    "pthread-affinity: worker=1 requested=0x2 actual=1 samples=100000",
                    "pthread-affinity: PASS workers=4 distinct=4",
                ]
            )
        )
        self.assertEqual(parsed["status"], "pass")
        self.assertEqual(parsed["metrics"]["workers"], 4)
        self.assertEqual(parsed["metrics"]["distinct"], 4)
        self.assertEqual(
            parsed["metrics"]["placements"],
            [
                {"worker": 0, "requested": "0x1", "actual": 0, "samples": 100000},
                {"worker": 1, "requested": "0x2", "actual": 1, "samples": 100000},
            ],
        )

    def test_parse_fail_log_rejects_wrong_lcpu(self) -> None:
        module = _load_pthread_affinity()
        self.assertIsNotNone(module, "scripts/app-pthread-affinity.py must exist")
        with self.assertRaises(ValueError):
            module.parse_probe_log(
                "\n".join(
                    [
                        "pthread-affinity: worker=1 requested=0x2 actual=0 samples=1",
                        "pthread-affinity: FAIL reason=wrong-lcpu",
                    ]
                )
            )


if __name__ == "__main__":
    unittest.main()

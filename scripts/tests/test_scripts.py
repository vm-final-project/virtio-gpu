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
eval_matrix = _load("eval_matrix", "eval_matrix.py")


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

    def test_eval_matrix_counts_blocked_without_passing_it(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            passed = root / "llama_cpu.json"
            blocked = root / "blocked.json"
            invalid = root / "invalid.json"
            passed.write_text('{"status": "pass"}\n')
            blocked.write_text('{"status": "blocked:qemu-missing"}\n')
            invalid.write_text('{"status": "failed"}\n')

            rc, summary = eval_matrix.check_results([passed, blocked])
            self.assertEqual(rc, 1)
            self.assertEqual(len(summary["invalid"]), 1)
            self.assertEqual(len(summary["blocked"]), 1)

            rc, summary = eval_matrix.check_results([passed, blocked, invalid])
            self.assertEqual(rc, 1)
            self.assertEqual(len(summary["invalid"]), 2)

    def test_eval_matrix_accepts_upstream_llama_bench_schema(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            row = Path(directory) / "llama_upstream_bench_cpu.json"
            row.write_text(
                '{"status":"pass","inputs":{"bench_kind":"upstream_llama_bench"},'
                '"metrics":{"bench_kind":"upstream_llama_bench","backend":"cpu",'
                '"threads":4,"pp":100.0,"tg":20.0}}\n'
            )

            rc, summary = eval_matrix.check_results([row])
            self.assertEqual(rc, 0)
            self.assertEqual(len(summary["pass"]), 1)

    def test_eval_matrix_rejects_vk_pass_without_renderer_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            row = Path(directory) / "llama_vk.json"
            row.write_text(
                '{"status":"pass","inputs":{"bench_kind":"hand_rolled_smoke"},'
                '"metrics":{"bench_kind":"hand_rolled_smoke","backend":"vulkan"}}\n'
            )

            rc, summary = eval_matrix.check_results([row])
            self.assertEqual(rc, 1)
            self.assertIn("qemu_mem_mb", summary["invalid"][0]["error"])

    def test_eval_matrix_accepts_vk_pass_with_renderer_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            row = Path(directory) / "llama_vk.json"
            row.write_text(
                '{"status":"pass",'
                '"inputs":{"bench_kind":"hand_rolled_smoke","backend":"vulkan",'
                '"qemu_mem_mb":4096,"gpu_hostmem":"4G",'
                '"egl_rendernode":"/dev/dri/renderD128",'
                '"renderer_expect":"hardware","model_size_bytes":1234},'
                '"metrics":{"bench_kind":"hand_rolled_smoke","backend":"vulkan"}}\n'
            )

            rc, summary = eval_matrix.check_results([row])
            self.assertEqual(rc, 0)
            self.assertEqual(len(summary["pass"]), 1)

    def test_eval_matrix_release_rejects_hand_rolled_vk_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            row = Path(directory) / "llama_vk.json"
            row.write_text(
                '{"status":"pass",'
                '"inputs":{"bench_kind":"hand_rolled_smoke","backend":"vulkan",'
                '"qemu_mem_mb":4096,"gpu_hostmem":"4G",'
                '"egl_rendernode":"auto","renderer_expect":"any",'
                '"model_size_bytes":1234},'
                '"metrics":{"bench_kind":"hand_rolled_smoke","backend":"vulkan"}}\n'
            )

            rc, summary = eval_matrix.check_results([row], release=True)
            self.assertEqual(rc, 1)
            self.assertIn("upstream_llama_bench", summary["invalid"][0]["error"])

    def test_eval_matrix_release_accepts_upstream_vk_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            row = Path(directory) / "llama_upstream_bench_vk.json"
            row.write_text(
                '{"status":"pass",'
                '"inputs":{"bench_kind":"upstream_llama_bench","backend":"vulkan",'
                '"qemu_mem_mb":4096,"gpu_hostmem":"4G",'
                '"egl_rendernode":"auto","renderer_expect":"any",'
                '"model_size_bytes":1234},'
                '"metrics":{"bench_kind":"upstream_llama_bench","backend":"vulkan",'
                '"threads":1,"pp":11.5,"tg":3.25}}\n'
            )

            rc, summary = eval_matrix.check_results([row], release=True)
            self.assertEqual(rc, 0)
            self.assertEqual(len(summary["pass"]), 1)

    def test_eval_matrix_accepts_matrix_summary_pass_row(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            row = Path(directory) / "llama_vk_matrix.json"
            row.write_text(
                '{"status":"pass","metrics":{"rows":2},'
                '"rows":[{"status":"pass"},{"status":"pass"}]}\n'
            )

            rc, summary = eval_matrix.check_results([row], release=True)
            self.assertEqual(rc, 0)
            self.assertEqual(len(summary["pass"]), 1)

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

    def test_cpu_upstream_bench_selects_distinct_image(self) -> None:
        cmd = llama_cpu.qemu_command(
            "qemu", Path("model"), "bench", 10, "x86_64",
            bench_kind="upstream_llama_bench",
        )
        self.assertIn("vogue-llama-cpu-upstream-bench_qemu-x86_64", " ".join(cmd))

    def test_vk_modes_select_distinct_images(self) -> None:
        bench = llama_vk.qemu_command("qemu", Path("model"), "bench", 10, 18080, "x86_64")
        server = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64")
        self.assertIn("vogue-llama-vk_qemu-x86_64", " ".join(bench))
        self.assertIn("vogue-llama-vk-server_qemu-x86_64", " ".join(server))
        self.assertNotIn("hostfwd", " ".join(bench))
        self.assertIn("hostfwd", " ".join(server))

    def test_vk_upstream_bench_selects_distinct_image(self) -> None:
        cmd = llama_vk.qemu_command(
            "qemu", Path("model"), "bench", 10, 18080, "x86_64",
            bench_kind="upstream_llama_bench",
        )
        self.assertIn("vogue-llama-vk-upstream-bench_qemu-x86_64", " ".join(cmd))

    def test_vk_matrix_rows_cover_small_large_and_memory_settings(self) -> None:
        matrix = _load("llama_vk_matrix", "llama_vk_matrix.py")

        rows = matrix.plan_rows(
            small_model="models/small.gguf",
            large_model="models/large.gguf",
            qemu_mem_mb=(4096, 8192),
            gpu_hostmem=("4G", "8G"),
        )
        labels = {
            (row["model_label"], row["qemu_mem_mb"], row["gpu_hostmem"])
            for row in rows
        }

        self.assertIn(("small", 4096, "4G"), labels)
        self.assertIn(("large", 8192, "8G"), labels)
        self.assertEqual(len(rows), 8)

    def test_vk_matrix_summary_blocks_when_any_row_blocks(self) -> None:
        matrix = _load("llama_vk_matrix", "llama_vk_matrix.py")

        summary = matrix.summarize_rows([
            {"status": "pass"},
            {"status": "blocked:image-missing"},
        ])

        self.assertEqual(summary["status"], "blocked:matrix-has-blocked")
        self.assertEqual(summary["metrics"]["pass"], 1)
        self.assertEqual(summary["metrics"]["blocked"], 1)

    def test_vk_smp_flag_present_only_when_multivcpu(self) -> None:
        one = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64", smp=1)
        four = llama_vk.qemu_command("qemu", Path("model"), "server", 10, 18080, "x86_64", smp=4)
        self.assertNotIn("-smp", one)
        self.assertEqual(four[four.index("-smp") + 1], "4")

    def test_vk_qemu_command_records_memory_hostmem_and_rendernode(self) -> None:
        env = {
            "VOGUE_QEMU_MEM_MB": "4096",
            "VOGUE_GPU_HOSTMEM": "4G",
            "VOGUE_EGL_RENDERNODE": "/dev/dri/renderD128",
        }
        cmd = llama_vk.qemu_command(
            "qemu", Path("model"), "bench", 10, 0, "x86_64", smp=1, env=env
        )
        joined = " ".join(cmd)
        self.assertIn("-m 4096", joined)
        self.assertIn("hostmem=4G,blob=true,venus=true", joined)
        self.assertIn("egl-headless,gl=on,rendernode=/dev/dri/renderD128", joined)

    def test_vk_renderer_info_classifies_llvmpipe_as_software(self) -> None:
        info = llama_vk.classify_renderer_text("renderer: llvmpipe (LLVM 18.1.8)")
        self.assertEqual(info["host_renderer_type"], "software")
        self.assertIn("llvmpipe", info["host_renderer_name"])

    def test_vk_renderer_info_classifies_lavapipe_as_software(self) -> None:
        info = llama_vk.classify_renderer_text("GPU id : 0 (llvmpipe / lavapipe)")
        self.assertEqual(info["host_renderer_type"], "software")

    def test_vk_renderer_info_classifies_radv_as_hardware(self) -> None:
        info = llama_vk.classify_renderer_text("GPU id : 0 (AMD Radeon RX 7900 XT (RADV NAVI31))")
        self.assertEqual(info["host_renderer_type"], "hardware")
        self.assertIn("RADV", info["host_renderer_name"])

    def test_vk_renderer_expect_hardware_blocks_software(self) -> None:
        status = llama_vk.renderer_expect_status(
            {"renderer_expect": "hardware", "host_renderer_type": "software"}
        )
        self.assertEqual(status, "blocked:renderer-expect-mismatch")

    def test_vk_host_renderer_probe_accepts_explicit_renderer_name(self) -> None:
        info = llama_vk.host_renderer_probe({"VOGUE_HOST_RENDERER_NAME": "llvmpipe"})
        self.assertEqual(info["host_renderer_type"], "software")
        self.assertEqual(info["host_renderer_name"], "llvmpipe")

    def test_vk_bench_parser_records_hand_rolled_kind(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: hand_rolled_smoke",
            "uk-llama-upstream-vk: pp512=10.0 tg128=2.0",
            "uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk",
        ])
        parsed = llama_vk.parse_run_log(log, "bench", {})
        self.assertTrue(parsed["passed"])
        self.assertEqual(parsed["status"], "pass")
        self.assertEqual(parsed["metrics"]["bench_kind"], "hand_rolled_smoke")

    def test_vk_runtime_blocker_overrides_pass_marker(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: hand_rolled_smoke",
            "uk-llama-upstream-vk: pp512=10.0 tg128=2.0",
            "uk-llama-vk-blocked: blocked:venus-query-memory-properties",
            "uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk",
        ])
        parsed = llama_vk.parse_run_log(log, "bench", {})
        self.assertFalse(parsed["passed"])
        self.assertEqual(parsed["status"], "blocked:venus-query-memory-properties")
        self.assertEqual(parsed["metrics"]["pp512"], 10.0)

    def test_vk_physical_properties_blocker_overrides_pass_marker(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: hand_rolled_smoke",
            "uk-llama-upstream-vk: pp512=10.0 tg128=2.0",
            "uk-ggml-vk: blocked:venus-query-physical-device-properties",
            "uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk",
        ])
        parsed = llama_vk.parse_run_log(log, "bench", {})
        self.assertFalse(parsed["passed"])
        self.assertEqual(parsed["status"], "blocked:venus-query-physical-device-properties")
        self.assertEqual(parsed["metrics"]["tg128"], 2.0)

    def test_vk_buffer_requirements_blocker_overrides_pass_marker(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: hand_rolled_smoke",
            "uk-llama-upstream-vk: pp512=10.0 tg128=2.0",
            "uk-ggml-vk: blocked:venus-query-buffer-requirements",
            "uk-ggml-vk: diagnostic:buffer-memory-requirements=tracked-local",
            "uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk",
        ])
        parsed = llama_vk.parse_run_log(log, "bench", {})
        self.assertFalse(parsed["passed"])
        self.assertEqual(parsed["status"], "blocked:venus-query-buffer-requirements")
        self.assertEqual(parsed["metrics"]["pp512"], 10.0)

    def test_vk_generic_vulkan_blocker_overrides_pass_marker(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: hand_rolled_smoke",
            "uk-llama-upstream-vk: pp512=10.0 tg128=2.0",
            "uk-vulkan: blocked:venus-reply-timeout",
            "uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk",
        ])
        parsed = llama_vk.parse_run_log(log, "bench", {})
        self.assertFalse(parsed["passed"])
        self.assertEqual(parsed["status"], "blocked:venus-reply-timeout")

    def test_vk_server_parser_blocks_even_if_http_ready(self) -> None:
        log = "\n".join([
            "uk-llama-upstream-vk-server: READY",
            "uk-ggml-vk: blocked:venus-query-physical-device-properties",
        ])
        parsed = llama_vk.parse_run_log(
            log,
            "server",
            {"http_status": 200, "completion_status": 200},
        )
        self.assertFalse(parsed["passed"])
        self.assertEqual(parsed["status"], "blocked:venus-query-physical-device-properties")

    def test_vk_upstream_llama_bench_jsonl_is_parsed(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: upstream_llama_bench",
            '{"n_prompt":512,"n_gen":0,"n_threads":1,"avg_ts":11.5}',
            '{"n_prompt":0,"n_gen":128,"n_threads":1,"avg_ts":3.25}',
            "uk-llama-upstream-vk: PASS evidence_id=llama-upstream-vk bench_kind=upstream_llama_bench",
        ])
        parsed = llama_vk.parse_run_log(log, "bench", {})
        self.assertTrue(parsed["passed"])
        self.assertEqual(parsed["metrics"]["bench_kind"], "upstream_llama_bench")
        self.assertEqual(parsed["metrics"]["backend"], "vulkan")
        self.assertEqual(parsed["metrics"]["threads"], 1)
        self.assertEqual(parsed["metrics"]["pp"], 11.5)
        self.assertEqual(parsed["metrics"]["tg"], 3.25)

    def test_cpu_smp_flag_present_only_when_multivcpu(self) -> None:
        one = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "x86_64", smp=1)
        four = llama_cpu.qemu_command("qemu", Path("model"), "bench", 10, "x86_64", smp=4)
        self.assertNotIn("-smp", one)
        self.assertEqual(four[four.index("-smp") + 1], "4")

    def test_result_path_keeps_x86_and_suffixes_arm64(self) -> None:
        root = Path("/tmp/results")
        self.assertEqual(common.result_path(root, "llama_cpu.json", "x86_64"), root / "llama_cpu.json")
        self.assertEqual(common.result_path(root, "llama_cpu.json", "arm64"), root / "llama_cpu_arm64.json")

    def test_llama_cpu_bench_requires_pass_marker_even_with_kernel_placement(self) -> None:
        log = "\n".join(
            [
                "[    5.109477] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x1 on-lcpu=1 sched-lcpu=1 ready=1",
                "[    5.122071] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x2 on-lcpu=2 sched-lcpu=2 ready=1",
                "uk-llama-upstream: pp512=100.0 tg128=20.0",
            ]
        )
        parsed = llama_cpu.parse_run_log(log, "bench", 4, {})

        self.assertFalse(parsed["passed"])
        self.assertEqual(parsed["status"], "blocked:no-pass-marker")
        self.assertEqual(parsed["metrics"]["placement_actuals_seen"], [1, 2])
        self.assertFalse(parsed["metrics"]["placement_complete"])
        self.assertEqual(parsed["metrics"]["pp512"], 100.0)
        self.assertEqual(parsed["metrics"]["tg128"], 20.0)

    def test_llama_cpu_bench_passes_only_with_pass_marker(self) -> None:
        log = "\n".join(
            [
                "uk-llama-bench-kind: hand_rolled_smoke",
                "uk-llama-upstream: workers=4 cpu_mask_bits=4 strict_cpu=1 poll=100",
                "uk-llama-upstream: pp512=100.0 tg128=20.0",
                "uk-llama-upstream: PASS evidence_id=llama-upstream-cpu",
            ]
        )
        parsed = llama_cpu.parse_run_log(log, "bench", 4, {})
        self.assertTrue(parsed["passed"])
        self.assertEqual(parsed["status"], "pass")
        self.assertEqual(parsed["metrics"]["ggml_workers"], 4)
        self.assertEqual(parsed["metrics"]["ggml_cpu_mask_bits"], 4)
        self.assertEqual(parsed["metrics"]["bench_kind"], "hand_rolled_smoke")

    def test_hand_rolled_smoke_is_not_reported_as_upstream_llama_bench(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: hand_rolled_smoke",
            "uk-llama-upstream: pp512=10.0 tg128=2.0",
            "uk-llama-upstream: PASS evidence_id=llama-upstream-cpu",
        ])
        parsed = llama_cpu.parse_run_log(log, "bench", 1, {})
        self.assertEqual(parsed["metrics"]["bench_kind"], "hand_rolled_smoke")

    def test_cpu_upstream_llama_bench_jsonl_is_parsed(self) -> None:
        log = "\n".join([
            "uk-llama-bench-kind: upstream_llama_bench",
            '{"n_prompt":512,"n_gen":0,"n_threads":4,"avg_ts":100.5}',
            '{"n_prompt":0,"n_gen":128,"n_threads":4,"avg_ts":20.25}',
            "uk-llama-upstream: PASS evidence_id=llama-upstream-cpu bench_kind=upstream_llama_bench",
        ])
        parsed = llama_cpu.parse_run_log(log, "bench", 4, {})
        self.assertTrue(parsed["passed"])
        self.assertEqual(parsed["metrics"]["bench_kind"], "upstream_llama_bench")
        self.assertEqual(parsed["metrics"]["backend"], "cpu")
        self.assertEqual(parsed["metrics"]["threads"], 4)
        self.assertEqual(parsed["metrics"]["pp"], 100.5)
        self.assertEqual(parsed["metrics"]["tg"], 20.25)

    def test_llama_server_metrics_can_include_ready_tps_and_kernel_placements(self) -> None:
        log = "\n".join(
            [
                "uk-llama-upstream-server: READY model=/mnt/model/model.gguf threads=4 slots=1 prompt_cache=1 mode=single-app no_fork_exec=1",
                "[    5.109477] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x1 on-lcpu=1 sched-lcpu=1 ready=1",
                "[    5.122071] Info: [libukschedcoop] <schedcoop.c @  171> SMPPLACE dequeue thread=0x2 on-lcpu=2 sched-lcpu=2 ready=1",
            ]
        )
        metrics = {"http_status": 200, "completion_status": 200, "server_toks_per_s": 42.5}
        parsed = llama_cpu.parse_run_log(log, "server", 4, metrics)

        self.assertTrue(parsed["passed"])
        self.assertEqual(
            parsed["metrics"],
            {
                "http_status": 200,
                "completion_status": 200,
                "server_toks_per_s": 42.5,
                "ready": "uk-llama-upstream-server: READY model=/mnt/model/model.gguf threads=4 slots=1 prompt_cache=1 mode=single-app no_fork_exec=1",
                "kernel_placements": [
                    {"actual": 1, "target": 1},
                    {"actual": 2, "target": 2},
                ],
                "placement_actuals_seen": [1, 2],
                "placement_expected": [0, 1, 2, 3],
                "placement_complete": False,
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
    def test_schedcoop_patch_exports_current_thread_placement_helper(self) -> None:
        patch = (
            Path(__file__).resolve().parents[2]
            / "patches/unikraft/0004-smp-per-lcpu-coop-scheduler.patch"
        ).read_text()

        self.assertIn(
            "int uk_schedcoop_smp_place_current(unsigned int target_lcpu);",
            patch,
        )
        self.assertIn(
            "int uk_schedcoop_smp_place_current(unsigned int target_lcpu)",
            patch,
        )
        self.assertIn("thread->affinity_target_lcpu = target_lcpu;", patch)
        self.assertIn("thread->affinity_migrate_pending =", patch)
        self.assertIn("SMPPLACE dequeue", patch)

    def test_affinity_migration_patch_remains_current_thread_compatible(self) -> None:
        patch = (
            Path(__file__).resolve().parents[2]
            / "patches/unikraft/0005-smp-per-thread-affinity-migration.patch"
        ).read_text()
        self.assertIn("UK_SYSCALL_R_E_DEFINE(int, sched_setaffinity", patch)
        self.assertIn("target = uk_sched_affinity_target(&effective, current_owner);", patch)
        self.assertIn("thread->affinity_target_lcpu = (unsigned int)target;", patch)
        self.assertIn("SMPPLACE setaffinity", patch)

    def test_llama_cpu_bench_uses_strict_one_worker_per_vcpu_threadpool(self) -> None:
        src = (
            Path(__file__).resolve().parents[2]
            / "apps/app-llama-cpu/bench.cpp"
        ).read_text()
        self.assertIn("uk_llama_cpu_online_vcpus()", src)
        self.assertIn("cparams.n_threads        = (int)nvcpu;", src)
        self.assertIn("cparams.n_threads_batch  = (int)nvcpu;", src)
        self.assertIn("ggml_threadpool_params_default((int)nvcpu)", src)
        self.assertIn("tpp.cpumask[i] = (i < nvcpu);", src)
        self.assertIn("tpp.strict_cpu = true;", src)
        self.assertIn("tpp.poll       = 100;", src)
        self.assertIn("uk-llama-upstream: workers=%u cpu_mask_bits=%u strict_cpu=1 poll=100", src)

    def test_llama_cpu_server_passes_upstream_placement_args_directly(self) -> None:
        src = (
            Path(__file__).resolve().parents[2]
            / "apps/app-llama-cpu/llama-server-entry.cpp"
        ).read_text()
        self.assertIn("return llama_server((int)(sizeof(argv) / sizeof(argv[0])), argv);", src)
        self.assertIn("static char threads_f[]  = \"--threads\";", src)
        self.assertIn("static char cpumask_f[]  = \"--cpu-mask\";", src)
        self.assertIn("static char strict_f[]   = \"--cpu-strict\";", src)
        self.assertIn("static char poll_f[]     = \"--poll\";", src)
        self.assertIn("cpu_mask=%s cpu_strict=1 poll=100", src)
        self.assertNotIn("fork(", src)
        self.assertNotIn("exec(", src)
        self.assertNotIn("system(", src)

    def test_cpu_kraftfiles_parameterize_worker_count_from_vogue_smp(self) -> None:
        root = Path(__file__).resolve().parents[2] / "kraft"
        bench = (root / "Kraftfile.llama-cpu-bench").read_text()
        server = (root / "Kraftfile.llama-cpu-server").read_text()
        upstream = (root / "Kraftfile.llama-cpu-upstream-bench").read_text()

        self.assertIn("CONFIG_APP_LLAMA_CPU_THREADS: '@@VOGUE_SMP@@'", bench)
        self.assertIn("CONFIG_UKPLAT_CPU_MAXCOUNT: '4'", bench)
        self.assertIn("CONFIG_APP_LLAMA_CPU_THREADS: '@@VOGUE_SMP@@'", server)
        self.assertIn("CONFIG_UKPLAT_CPU_MAXCOUNT: '4'", server)
        self.assertIn("CONFIG_APP_LLAMA_CPU_BENCH_UPSTREAM: 'y'", upstream)
        self.assertIn("CONFIG_APP_LLAMA_CPU_THREADS: '@@VOGUE_SMP@@'", upstream)

    def test_upstream_llama_bench_targets_are_real_build_targets(self) -> None:
        root = Path(__file__).resolve().parents[2]
        mk = (root / "mk/llama.mk").read_text()
        cpu_bench = (root / "apps/app-llama-cpu/bench.cpp").read_text()
        vk_bench = (root / "apps/app-llama-vk/bench.cpp").read_text()

        self.assertIn("llama-cpu-upstream-bench-build", mk)
        self.assertIn("llama-vk-upstream-bench-build", mk)
        self.assertIn("CONFIG_APP_LLAMA_CPU_BENCH_UPSTREAM", cpu_bench)
        self.assertIn("CONFIG_APP_LLAMA_VK_BENCH_UPSTREAM", vk_bench)
        self.assertIn("llama_bench(", cpu_bench)
        self.assertIn("llama_bench(", vk_bench)

    def test_root_makefile_defaults_to_arm64_on_apple_silicon(self) -> None:
        root = Path(__file__).resolve().parents[2]
        mk = (root / "Makefile").read_text()

        self.assertIn("HOST_UNAME_S", mk)
        self.assertIn("HOST_UNAME_M", mk)
        self.assertIn("DEFAULT_ARCH", mk)
        self.assertIn("Darwin arm64", mk)
        self.assertIn("ARCH ?= $(DEFAULT_ARCH)", mk)
        self.assertNotIn("ARCH ?= x86_64", mk)

    def test_root_makefile_uses_host_aware_vulkan_library_default(self) -> None:
        root = Path(__file__).resolve().parents[2]
        mk = (root / "Makefile").read_text()

        self.assertIn("DEFAULT_VK_LIB", mk)
        self.assertIn("/opt/homebrew/lib/libvulkan.dylib", mk)
        self.assertIn("/usr/local/lib/libvulkan.dylib", mk)
        self.assertIn("VK_LIB                 ?= $(DEFAULT_VK_LIB)", mk)
        self.assertNotIn("VK_LIB                 ?= /usr/lib/x86_64-linux-gnu/libvulkan.so.1", mk)

    def test_vk_prepare_reports_precise_host_dependency_blockers(self) -> None:
        root = Path(__file__).resolve().parents[2]
        mk = (root / "mk/llama.mk").read_text()

        self.assertIn("blocked:missing-glslc", mk)
        self.assertIn("GLSLC=/path/to/glslc", mk)
        self.assertIn("blocked:missing-vulkan-library", mk)
        self.assertIn("blocked:unsupported-vogue-march", mk)

    def test_vk_prepare_uses_arm64_safe_mtune_for_march_probe(self) -> None:
        root = Path(__file__).resolve().parents[2]
        root_mk = (root / "Makefile").read_text()
        llama_mk = (root / "mk/llama.mk").read_text()

        self.assertIn("VOGUE_MTUNE", root_mk)
        self.assertIn("arm64),generic", root_mk)
        self.assertIn("-mtune=$(VOGUE_MTUNE)", llama_mk)
        self.assertNotIn("-mtune=$(VOGUE_MARCH)", llama_mk)

    def test_vk_prepare_passes_explicit_glslc_to_upstream_cmake(self) -> None:
        root = Path(__file__).resolve().parents[2]
        root_mk = (root / "Makefile").read_text()
        llama_mk = (root / "mk/llama.mk").read_text()

        self.assertIn("GLSLC", root_mk)
        self.assertIn("COMPILER GLSLC", root_mk)
        self.assertIn("-DVulkan_GLSLC_EXECUTABLE=$(GLSLC)", llama_mk)

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

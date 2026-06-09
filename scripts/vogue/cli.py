"""Unified command line interface for VOGUE automation."""
from __future__ import annotations

import argparse
import sys
from collections.abc import Callable

from .commands import (
    current_stage,
    eval_matrix,
    linux_guest_vulkan_baseline,
    llama_cpu_run,
    llama_host_vulkan_baseline,
    llama_server_cpu,
    llama_server_vk,
    llama_vk_build,
    llama_vk_run,
    server_vk_check,
    server_vk_throughput,
    venus_perf,
    venus_probe,
    vulkan_perf,
)

Command = tuple[Callable[..., int], list[str]]


def _command(args: argparse.Namespace) -> Command:
    commands: dict[tuple[str, str], Callable[..., int]] = {
        ("capture", "cpu-bench"): llama_cpu_run.main,
        ("capture", "cpu-server"): llama_server_cpu.main,
        ("capture", "vk-bench"): llama_vk_run.main,
        ("capture", "vk-server"): llama_server_vk.main,
        ("capture", "vk-build"): llama_vk_build.main,
        ("capture", "linux-guest-vk"): linux_guest_vulkan_baseline.main,
        ("capture", "host-vk"): llama_host_vulkan_baseline.main,
        ("probe", "venus"): venus_probe.main,
        ("evaluate", "venus"): venus_perf.main,
        ("evaluate", "vulkan"): vulkan_perf.main,
        ("evaluate", "server-vk"): server_vk_check.main,
        ("evaluate", "server-vk-throughput"): server_vk_throughput.main,
        ("evaluate", "matrix"): eval_matrix.main,
        ("report", "stage"): current_stage.main,
    }
    return commands[(args.group, args.command)], args.command_args


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python3 -m scripts.vogue")
    subparsers = parser.add_subparsers(dest="group", required=True)
    choices = {
        "capture": (
            "cpu-bench", "cpu-server", "vk-bench", "vk-server", "vk-build",
            "linux-guest-vk", "host-vk",
        ),
        "probe": ("venus",),
        "evaluate": ("venus", "vulkan", "server-vk", "server-vk-throughput", "matrix"),
        "report": ("stage",),
    }
    for group, commands in choices.items():
        group_parser = subparsers.add_parser(group)
        group_parser.add_argument("command", choices=commands)
        group_parser.add_argument("command_args", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command, command_args = _command(args)
    if command.__module__.endswith(("server_vk_check", "server_vk_throughput")):
        return command(command_args)
    previous = sys.argv
    try:
        sys.argv = [f"{args.group} {args.command}", *command_args]
        return command()
    finally:
        sys.argv = previous

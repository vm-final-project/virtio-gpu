from __future__ import annotations

import argparse
import unittest

from scripts.vogue import cli
from scripts.vogue.core.artifacts import blocked_artifact, rows_by_status


class CliTests(unittest.TestCase):
    def test_every_public_command_resolves(self) -> None:
        commands = {
            "capture": (
                "cpu-bench", "cpu-server", "vk-bench", "vk-server", "vk-build",
                "linux-guest-vk", "host-vk",
            ),
            "probe": ("venus",),
            "evaluate": ("venus", "vulkan", "server-vk", "server-vk-throughput", "matrix"),
            "report": ("stage",),
        }
        for group, names in commands.items():
            for name in names:
                command, command_args = cli._command(
                    argparse.Namespace(group=group, command=name, command_args=["--check"])
                )
                self.assertTrue(callable(command))
                self.assertEqual(command_args, ["--check"])

    def test_blocked_artifact_has_consistent_schema(self) -> None:
        payload = blocked_artifact(
            source="test",
            status="blocked:missing",
            headline="missing input",
            stage="artifact-missing",
            first_missing_dependency="input.json",
            claim_allowed="blocker only",
            claim_forbidden="pass",
            next_step="create input",
        )
        self.assertEqual(payload["status"], "blocked:missing")
        self.assertEqual(payload["block"]["first_missing_dependency"], "input.json")
        self.assertEqual(payload["rows"], [])

    def test_rows_by_status_is_stable(self) -> None:
        rows = [{"status": "pass"}, {"status": "blocked:x"}, {"status": "pass"}]
        self.assertEqual(rows_by_status(rows), {"blocked:x": 1, "pass": 2})


if __name__ == "__main__":
    unittest.main()

"""Minimal self-running test harness (the repo has no pytest).

Usage in a test module:

    from _runner import run
    def test_something():
        assert ...
    if __name__ == "__main__":
        run(globals())

`run` executes every top-level callable named ``test_*`` in declaration order,
prints PASS/FAIL per test, and exits non-zero if any test fails.
"""
from __future__ import annotations
import sys
import traceback


def run(ns: dict) -> None:
    tests = [(name, fn) for name, fn in ns.items()
             if name.startswith("test_") and callable(fn)]
    # preserve declaration order (dict is insertion-ordered in py3.7+)
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print(f"PASS {name}")
        except Exception as exc:  # noqa: BLE001
            failed += 1
            print(f"FAIL {name}: {exc}")
            traceback.print_exc()
    total = len(tests)
    print(f"--- {total - failed}/{total} passed ---")
    sys.exit(1 if failed else 0)

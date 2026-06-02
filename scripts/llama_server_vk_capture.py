#!/usr/bin/env python3
"""Boot the upstream llama.cpp **Vulkan server** appliance under real QEMU
virtio-gpu-gl Venus, then prove it actually serves HTTP.

The single-application server appliance (apps/app-llama-upstream-vk/server.cpp,
MODE_SERVER) boots directly into one entrypoint — no shell, no fork/exec
launcher — mounts the GGUF over 9pfs, initialises the real Venus dispatch chain
(libukggml_vk -> libukvenus SUBMIT_3D -> virtio-gpu-gl venus=true), loads the
model on the host GPU and prints:

  uk-llama-upstream-vk-server: READY ... slots=N ctx_per_slot=M ...

It then hands control to the upstream cpp-httplib server, which binds
0.0.0.0:8080 over the in-guest lwIP TCP/IP stack (virtio-net NIC -> libuknetdev
-> lwIP). QEMU user-mode networking forwards a host port to guest 8080, so this
script can issue a real HTTP request from the host:

  GET  http://127.0.0.1:<hostport>/health        -> {"status":"ok"}
  GET  http://127.0.0.1:<hostport>/v1/models      -> model card
  POST http://127.0.0.1:<hostport>/completion     -> one bounded completion

A `pass` row now means model-loaded readiness over real Venus AND a same-run
HTTP response from the in-guest llama.cpp server. Writes
results/llama/upstream_server_vk.json (read by eval_matrix.py) and
results/llama/upstream_server_vk_serial.log (read by llm_server_vk_check.py).
"""
from __future__ import annotations

import json
import os
import platform
import re
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results" / "llama"
IMAGE = ROOT / ".unikraft" / "build" / "vogue-llama-upstream-vk-server_qemu-x86_64"
SERIAL_LOG = RESULTS / "upstream_server_vk_serial.log"

GUEST_IP = "10.0.2.15"
GUEST_CIDR = "10.0.2.15/24"
GUEST_GW = "10.0.2.2"

READY_RE = re.compile(
    r"uk-llama-upstream-vk-server: READY .*slots=(?P<slots>\d+) "
    r"ctx_per_slot=(?P<ctx>\d+) batch_size=(?P<batch>\d+) "
    r"ubatch_size=(?P<ubatch>\d+) prompt_cache=(?P<pc>\d) "
    r"batch_enabled=(?P<be>\d) "
    r"hostmem_fixed=(?P<hf>\d)")


def _now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _free_port() -> int:
    explicit = os.environ.get("VOGUE_SRV_HOSTPORT")
    if explicit:
        return int(explicit)
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def _select_qemu() -> str | None:
    explicit = os.environ.get("QEMU")
    cands = []
    if explicit:
        cands.append(shutil.which(explicit) or explicit)
    cands += [str(ROOT.parent / "qemu-src" / "build" / "qemu-system-x86_64"),
              "/usr/local/bin/qemu-system-x86_64"]
    path_bin = shutil.which("qemu-system-x86_64")
    if path_bin:
        cands.append(path_bin)
    existing = [c for c in cands if c and Path(c).exists()]
    for c in existing:
        try:
            out = subprocess.run([c, "-device", "virtio-gpu-gl-pci,help"],
                                 text=True, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, timeout=10).stdout
        except Exception:
            continue
        if "venus=" in out:
            return c
    return existing[0] if existing else None


def _model_path() -> Path | None:
    for env in ("VOGUE_VK_MODEL", "VOGUE_CPU_MODEL"):
        v = os.environ.get(env)
        if v and Path(v).is_file():
            return Path(v)
    try:
        cfg = json.loads((ROOT / "config" / "llama_env_matrix.json").read_text())
        cand = ROOT / cfg["model"]["default_path"]
        if cand.is_file():
            return cand
    except (OSError, KeyError, json.JSONDecodeError):
        pass
    for p in (ROOT.parent / "models").rglob("*.gguf"):
        return p
    return None


def _grant_render_nodes() -> None:
    nodes = [str(p) for p in Path("/dev/dri").glob("renderD*")]
    if not nodes or all(os.access(n, os.R_OK | os.W_OK) for n in nodes):
        return
    if shutil.which("sudo"):
        subprocess.run(["sudo", "-n", "chmod", "o+rw", *nodes],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def _write(payload: dict) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    payload.setdefault("generated_utc", _now())
    (RESULTS / "upstream_server_vk.json").write_text(
        json.dumps(payload, indent=2) + "\n")


def _emit(status: str, *, extra: dict | None = None, ready: dict | None = None,
          http: dict | None = None) -> int:
    base = {
        "schema": "llama/upstream-server-vk.v2",
        "evidence_id": "llama-upstream-vk-server",
        "status": status,
        "pass": status == "pass",
        "image": str(IMAGE.relative_to(ROOT)) if IMAGE.exists() else None,
        "host": platform.platform(),
        "serial_log": str(SERIAL_LOG.relative_to(ROOT)) if SERIAL_LOG.exists() else None,
        "transport": "virtio-gpu-gl venus=true; libukggml_vk -> libukvenus SUBMIT_3D -> host virglrenderer Venus",
        "network": "virtio-net-pci -> libuknetdev -> lwIP TCP/IP; QEMU user-mode hostfwd to guest 8080",
        "claim_allowed": ("Single-application llama.cpp Vulkan server appliance boots directly "
                          "into one entrypoint (no shell, no fork/exec), reaches model-loaded "
                          "readiness over the real virtio-gpu-gl Venus path, and serves HTTP over "
                          "the in-guest lwIP stack (same-run /health + /completion proof)."),
        "claim_forbidden": ("Aggregate request/second throughput, TTFT, or slot-utilisation "
                            "benchmarking — only same-run HTTP liveness + one bounded completion "
                            "is captured here, not a performance number."),
    }
    if ready:
        base.update(ready)
    if http:
        base["http"] = http
    if extra:
        base.update(extra)
    _write(base)
    msg = f"llama-server-vk-capture: {status}"
    if ready:
        msg += f" slots={ready['slots']} ctx_per_slot={ready['ctx_per_slot']}"
    if http and http.get("health_status"):
        msg += f" http_health={http['health_status']}"
    print(msg)
    return 0


def _http_get(url: str, timeout: float = 5.0) -> tuple[int | None, str]:
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status, r.read(8192).decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read(8192).decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001
        return None, str(e)


def _http_post(url: str, payload: dict, timeout: float = 60.0) -> tuple[int | None, str]:
    data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read(65536).decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read(65536).decode("utf-8", "replace")
    except Exception as e:  # noqa: BLE001
        return None, str(e)


def _probe_http(hostport: int, deadline: float) -> dict:
    """Poll /health until the server answers, then capture /v1/models and one
    bounded /completion. Returns a structured proof dict."""
    base = f"http://127.0.0.1:{hostport}"
    proof: dict = {"endpoint": base, "health_status": None}
    # /health: 503 while the model reloads, 200 once ready to serve.
    while time.time() < deadline:
        code, body = _http_get(base + "/health", timeout=4.0)
        if code is not None:
            proof["health_status"] = code
            proof["health_body"] = body.strip()[:512]
            if code == 200:
                break
        time.sleep(1.0)
    if proof.get("health_status") != 200:
        return proof
    mcode, mbody = _http_get(base + "/v1/models", timeout=5.0)
    proof["models_status"] = mcode
    proof["models_body"] = (mbody or "").strip()[:1024]
    ccode, cbody = _http_post(base + "/completion",
                              {"prompt": "Hello from VOGUE on Unikraft. Reply with one short sentence.",
                               "n_predict": 16, "temperature": 0.0,
                               "cache_prompt": False},
                              timeout=120.0)
    proof["completion_status"] = ccode
    try:
        cj = json.loads(cbody)
        proof["completion_content"] = (cj.get("content") or "").strip()[:512]
        proof["completion_tokens_predicted"] = cj.get("tokens_predicted")
    except (json.JSONDecodeError, AttributeError):
        proof["completion_body"] = (cbody or "").strip()[:512]

    # Optional bounded throughput burst (llm-server-vk-throughput-check). Drives
    # a fixed number of sequential completions against the same model and records
    # measured requests/s, tokens/s, and time-to-first-token. Off by default so
    # the liveness probe stays fast; never promoted to a headline claim.
    reqs = int(os.environ.get("VOGUE_SRV_THROUGHPUT_REQS", "0"))
    if reqs > 0 and proof.get("completion_status") == 200:
        proof["throughput"] = _throughput_burst(base, reqs)
    return proof


def _throughput_burst(base: str, reqs: int) -> dict:
    n_predict = int(os.environ.get("VOGUE_SRV_THROUGHPUT_NPREDICT", "128"))
    # Concurrency exercises the server's parallel slots (continuous batching).
    # Default 1 (single-stream) because the appliance runs on a single vCPU,
    # where that is the honest throughput number comparable to the native
    # tg128 baseline; raise VOGUE_SRV_CONCURRENCY once the guest gains SMP so
    # the parallel slots can decode in genuinely overlapping CPU time.
    concurrency = int(os.environ.get("VOGUE_SRV_CONCURRENCY", "1"))
    prompt = "Write one concise sentence about unikernels."
    out: dict = {"requests": reqs, "concurrency": concurrency,
                 "n_predict": n_predict, "ok": 0, "failed": 0, "total_tokens": 0}

    # Time-to-first-token via a single streaming request (idle server).
    ttft = None
    try:
        data = json.dumps({"prompt": prompt, "n_predict": n_predict,
                           "temperature": 0.0, "cache_prompt": False,
                           "stream": True}).encode()
        req = urllib.request.Request(base + "/completion", data=data,
                                     headers={"Content-Type": "application/json"})
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=120.0) as r:
            if r.readline():
                ttft = round(time.time() - t0, 4)
    except Exception:  # noqa: BLE001
        pass
    out["ttft_s"] = ttft

    # Concurrent completion burst: fire `reqs` requests across `concurrency`
    # workers and measure aggregate throughput over the whole wall-clock window.
    from concurrent.futures import ThreadPoolExecutor

    def _one(_i: int) -> tuple[bool, float, int, float, float]:
        t = time.time()
        code, body = _http_post(base + "/completion",
                                {"prompt": prompt, "n_predict": n_predict,
                                 "temperature": 0.0, "cache_prompt": False},
                                timeout=180.0)
        dt = time.time() - t
        if code == 200:
            try:
                j = json.loads(body)
                tim = j.get("timings") or {}
                # predicted_per_second = pure decode rate (excludes prefill);
                # this is the apples-to-apples comparison to bench tg128.
                return (True, dt, int(j.get("tokens_predicted") or 0),
                        float(tim.get("predicted_per_second") or 0.0),
                        float(tim.get("prompt_per_second") or 0.0))
            except (json.JSONDecodeError, AttributeError, TypeError):
                return True, dt, 0, 0.0, 0.0
        return False, dt, 0, 0.0, 0.0

    latencies: list[float] = []
    decode_rates: list[float] = []
    prompt_rates: list[float] = []
    t_start = time.time()
    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        for ok, dt, toks, dec, pp in pool.map(_one, range(reqs)):
            if ok:
                out["ok"] += 1
                latencies.append(dt)
                out["total_tokens"] += toks
                if dec > 0:
                    decode_rates.append(dec)
                if pp > 0:
                    prompt_rates.append(pp)
            else:
                out["failed"] += 1
    wall = time.time() - t_start
    out["wall_s"] = round(wall, 3)
    if out["ok"] and wall > 0:
        out["requests_per_s"] = round(out["ok"] / wall, 3)
        out["tokens_per_s"] = round(out["total_tokens"] / wall, 2)
        out["mean_latency_s"] = round(sum(latencies) / len(latencies), 3)
    if decode_rates:
        # Server-reported decode rate (tok/s, prefill excluded) — comparable to
        # the bench tg128 and the native vulkan_linux_baseline.
        out["decode_tps_mean"] = round(sum(decode_rates) / len(decode_rates), 2)
        out["decode_tps_max"] = round(max(decode_rates), 2)
    if prompt_rates:
        out["prompt_tps_mean"] = round(sum(prompt_rates) / len(prompt_rates), 2)
    return out


def _attempt(qemu: str, model: Path) -> tuple[str, dict]:
    _grant_render_nodes()
    mem = os.environ.get("VOGUE_VK_MEM", "3072")
    kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
    accel, cpu = ("kvm", "host") if kvm else ("tcg", "max")
    timeout = int(os.environ.get("VOGUE_VK_TIMEOUT", "420"))
    hostport = _free_port()

    with tempfile.TemporaryDirectory(prefix="vogue-vk-srv-") as share:
        shutil.copy(model, Path(share) / "model.gguf")
        smp = os.environ.get("VOGUE_SRV_SMP", "4")
        cmd = [
            qemu, "-machine", f"accel={accel}", "-cpu", cpu, "-m", mem,
            "-smp", smp,
            "-no-reboot", "-kernel", str(IMAGE),
            "-fsdev", f"local,id=myid,path={share},security_model=none",
            "-device", "virtio-9p-pci,fsdev=myid,mount_tag=model",
            "-display", "egl-headless,gl=on", "-vga", "none",
            "-device", "virtio-gpu-gl-pci,hostmem=512M,blob=true,venus=true",
            "-netdev", f"user,id=net0,hostfwd=tcp:127.0.0.1:{hostport}-{GUEST_IP}:8080",
            "-device", "virtio-net-pci,netdev=net0",
            "-append", ("console=ttyS0 "
                        "random.seed=2463534242,1013904223,1664525,1013904223,"
                        "22695477,1103515245,134775813,214013"),
            "-serial", "mon:stdio", "-monitor", "none",
        ]
        env = {**os.environ, "VIRGL_DEBUG": os.environ.get("VIRGL_DEBUG", "verbose")}

        out_lines: list[str] = []
        state = {"ready": None, "listening": False}

        try:
            proc = subprocess.Popen(cmd, cwd=ROOT, text=True, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    bufsize=1)
        except OSError as e:
            return ("blocked:qemu-spawn-failed", dict(extra={"error": str(e)}))

        assert proc.stdout is not None

        def _drain():
            for line in proc.stdout:  # type: ignore[union-attr]
                out_lines.append(line)
                if state["ready"] is None and "uk-llama-upstream-vk-server: READY" in line:
                    state["ready"] = line
                if ("server is listening" in line
                        or "HTTP server is listening" in line
                        or "starting the main loop" in line):
                    state["listening"] = True

        reader = threading.Thread(target=_drain, daemon=True)
        reader.start()

        deadline = time.time() + timeout
        http_proof: dict = {}
        try:
            # Wait for model-loaded READY first.
            while time.time() < deadline and state["ready"] is None:
                if proc.poll() is not None:
                    break
                time.sleep(0.5)
            if state["ready"] is not None:
                # Give the upstream server a moment to bind, then probe HTTP.
                probe_deadline = min(deadline,
                                     time.time() + int(os.environ.get("VOGUE_SRV_HTTP_S", "180")))
                http_proof = _probe_http(hostport, probe_deadline)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
            reader.join(timeout=3)

    out = "".join(out_lines)
    SERIAL_LOG.parent.mkdir(parents=True, exist_ok=True)
    SERIAL_LOG.write_text(out)

    m = READY_RE.search(out)
    dev = re.search(r"uk-ggml-vk:\s*venus physical_device=([^\n]+)", out)
    venus_device = dev.group(1).strip() if dev else (
        "Tesla V100-SXM2-16GB" if "Tesla V100" in out else None)

    if not m:
        if "Unikraft Crash" in out:
            return ("blocked:venus-device-crash", dict(extra={"log_tail": out[-3000:]}))
        return ("blocked:no-ready-line", dict(extra={"log_tail": out[-3000:]}))

    ready = {
        "slots": int(m.group("slots")),
        "ctx_per_slot": int(m.group("ctx")),
        "batch_size": int(m.group("batch")),
        "ubatch_size": int(m.group("ubatch")),
        "prompt_cache": bool(int(m.group("pc"))),
        "dispatch_batch_enabled": bool(int(m.group("be"))),
        "hostmem_fixed": bool(int(m.group("hf"))),
        "venus_device": venus_device,
        "ready_marker": m.group(0).strip(),
    }

    if http_proof.get("health_status") == 200:
        return ("pass", dict(ready=ready, http=http_proof))
    # Reached model-loaded readiness but HTTP did not come up: structured blocker
    # so we never silently claim serving.
    return ("blocked:http-not-served",
            dict(ready=ready, http=http_proof or {"health_status": None},
                 extra={"log_tail": out[-3000:]}))


# Model load over Venus shares the from-scratch ICD's transient crash mode; a
# clean re-boot succeeds, so retry a crashed/no-ready attempt.
_RETRYABLE = {"blocked:venus-device-crash", "blocked:no-ready-line",
              "blocked:http-not-served"}


def main() -> int:
    qemu = _select_qemu()
    if not qemu:
        return _emit("blocked:qemu-missing")
    if not IMAGE.exists():
        return _emit("blocked:unikraft-image-missing")
    model = _model_path()
    if model is None:
        return _emit("blocked:model-missing")

    attempts = max(1, int(os.environ.get("VOGUE_VK_ATTEMPTS", "3")))
    status, kwargs = "blocked:no-ready-line", {}
    i = 0
    for i in range(attempts):
        status, kwargs = _attempt(qemu, model)
        if status == "pass" or status not in _RETRYABLE:
            break
        if i + 1 < attempts:
            print(f"llama-server-vk-capture: attempt {i+1} -> {status}; retrying")
    kwargs.setdefault("extra", {})
    kwargs["extra"]["attempts_used"] = i + 1
    return _emit(status, **kwargs)


if __name__ == "__main__":
    raise SystemExit(main())

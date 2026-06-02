#!/usr/bin/env bash
# Reference run script for the llm.server.vk appliance.
#
# Pins the QEMU device flags called out in plan-optimize.md L4.1/L4.2:
#   - virtio-gpu-gl-pci,hostmem=8G,blob=true,venus=true (Venus + host-visible blob)
#   - -display egl-headless,gl=on (no Wayland/X11, removes EGL render-node fallout)
#
# Networking (HTTP serving): a virtio-net NIC is backed by QEMU user-mode
# networking with a host-port forward to the guest's upstream llama.cpp HTTP
# listener on 8080. The guest gets a static IPv4 via the uknetdev EINFO libparam
# (netdev.ip=<cidr>:<gw>), parsed by lwIP at boot. After the READY line the
# server answers GET http://127.0.0.1:${HOSTPORT}/health and the OpenAI-style
# completion endpoints.
#
# Reviewers reproducing the appliance use this script as the canonical command;
# scripts/llm_server_vk_check.py asserts the exact same flags are present.
set -euo pipefail

QEMU=${QEMU:-qemu-system-x86_64}
IMAGE=${IMAGE:-.unikraft/build/vogue-llama-upstream-vk-server_qemu-x86_64}
HOSTMEM=${HOSTMEM:-8G}
MODEL_DIR=${MODEL_DIR:-rootfs/llama}
ACCEL=${ACCEL:-kvm}
MEM=${MEM:-4G}
HOSTPORT=${HOSTPORT:-18080}
GUEST_IP=${GUEST_IP:-10.0.2.15}
GUEST_CIDR=${GUEST_CIDR:-10.0.2.15/24}
GUEST_GW=${GUEST_GW:-10.0.2.2}

if [ ! -f "$IMAGE" ]; then
    echo "run-llm-server-vk: image missing: $IMAGE" >&2
    echo "build with: make llama-upstream-vk-server-build" >&2
    exit 2
fi
if [ ! -d "$MODEL_DIR" ]; then
    echo "run-llm-server-vk: model dir missing: $MODEL_DIR" >&2
    exit 2
fi

echo "run-llm-server-vk: HTTP server will be reachable at http://127.0.0.1:${HOSTPORT}/health" >&2

exec "$QEMU" \
    -machine "accel=$ACCEL" \
    -cpu max \
    -m "$MEM" \
    -kernel "$IMAGE" \
    -append "console=ttyS0 random.seed=2463534242,1013904223,1664525,1013904223,22695477,1103515245,134775813,214013" \
    -display egl-headless,gl=on \
    -device "virtio-gpu-gl-pci,hostmem=$HOSTMEM,blob=true,venus=true" \
    -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:${HOSTPORT}-${GUEST_IP}:8080" \
    -device "virtio-net-pci,netdev=net0" \
    -virtfs "local,path=$MODEL_DIR,mount_tag=model,security_model=none,id=model" \
    -serial mon:stdio

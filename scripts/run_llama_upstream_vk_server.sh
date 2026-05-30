#!/usr/bin/env bash
# Reference run script for the llm.server.vk appliance.
#
# Pins the QEMU device flags called out in plan-optimize.md L4.1/L4.2:
#   - virtio-gpu-gl-pci,hostmem=8G,blob=true,venus=true (Venus + host-visible blob)
#   - -display egl-headless,gl=on (no Wayland/X11, removes EGL render-node fallout)
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

if [ ! -f "$IMAGE" ]; then
    echo "run-llm-server-vk: image missing: $IMAGE" >&2
    echo "build with: make llama-upstream-vk-server-build" >&2
    exit 2
fi
if [ ! -d "$MODEL_DIR" ]; then
    echo "run-llm-server-vk: model dir missing: $MODEL_DIR" >&2
    exit 2
fi

exec "$QEMU" \
    -machine "accel=$ACCEL" \
    -cpu max \
    -m "$MEM" \
    -kernel "$IMAGE" \
    -display egl-headless,gl=on \
    -device "virtio-gpu-gl-pci,hostmem=$HOSTMEM,blob=true,venus=true" \
    -virtfs "local,path=$MODEL_DIR,mount_tag=model,security_model=none,id=model" \
    -serial mon:stdio

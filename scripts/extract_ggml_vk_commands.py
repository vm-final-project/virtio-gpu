#!/usr/bin/env python3
"""Deterministically derive the Vulkan command set ggml-vulkan/llama.cpp uses.

ggml-vulkan.cpp is written against vk-hpp (the C++ wrapper), so calls appear as
either C entry points (`vkCmdCopyBuffer`) or hpp methods (`.createBuffer`,
`buf.dispatch`). We map the hpp method spellings back to Vulkan command names
with a fixed table so the output is a stable, reviewable manifest.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

LLAMA_ROOT = Path(os.environ.get("LLAMA_ROOT", "../llama.cpp")).resolve()
SRC = LLAMA_ROOT / "ggml/src/ggml-vulkan/ggml-vulkan.cpp"

# vk-hpp method spelling -> Vulkan command. Reviewed against the live scan.
HPP_METHOD_TO_CMD = {
    "createInstance": "vkCreateInstance", "createDevice": "vkCreateDevice",
    "getQueue": "vkGetDeviceQueue", "getQueue2": "vkGetDeviceQueue2",
    "allocateMemory": "vkAllocateMemory", "freeMemory": "vkFreeMemory",
    "mapMemory": "vkMapMemory", "unmapMemory": "vkUnmapMemory",
    "bindBufferMemory": "vkBindBufferMemory",
    "getBufferMemoryRequirements": "vkGetBufferMemoryRequirements",
    "createBuffer": "vkCreateBuffer", "destroyBuffer": "vkDestroyBuffer",
    "createShaderModule": "vkCreateShaderModule",
    "destroyShaderModule": "vkDestroyShaderModule",
    "createComputePipeline": "vkCreateComputePipelines",
    "createComputePipelines": "vkCreateComputePipelines",
    "destroyPipeline": "vkDestroyPipeline",
    "createPipelineLayout": "vkCreatePipelineLayout",
    "destroyPipelineLayout": "vkDestroyPipelineLayout",
    "createDescriptorSetLayout": "vkCreateDescriptorSetLayout",
    "destroyDescriptorSetLayout": "vkDestroyDescriptorSetLayout",
    "createDescriptorPool": "vkCreateDescriptorPool",
    "destroyDescriptorPool": "vkDestroyDescriptorPool",
    "allocateDescriptorSets": "vkAllocateDescriptorSets",
    "updateDescriptorSets": "vkUpdateDescriptorSets",
    "createCommandPool": "vkCreateCommandPool",
    "destroyCommandPool": "vkDestroyCommandPool",
    "resetCommandPool": "vkResetCommandPool",
    "allocateCommandBuffers": "vkAllocateCommandBuffers",
    "bindPipeline": "vkCmdBindPipeline",
    "bindDescriptorSets": "vkCmdBindDescriptorSets",
    "pushConstants": "vkCmdPushConstants", "dispatch": "vkCmdDispatch",
    "copyBuffer": "vkCmdCopyBuffer", "fillBuffer": "vkCmdFillBuffer",
    "pipelineBarrier": "vkCmdPipelineBarrier",
    "createFence": "vkCreateFence", "destroyFence": "vkDestroyFence",
    "resetFences": "vkResetFences", "waitForFences": "vkWaitForFences",
    "getFenceStatus": "vkGetFenceStatus", "createEvent": "vkCreateEvent",
    "destroyEvent": "vkDestroyEvent", "resetEvent": "vkResetEvent",
    "createSemaphore": "vkCreateSemaphore",
    "destroySemaphore": "vkDestroySemaphore",
    "createQueryPool": "vkCreateQueryPool",
    "destroyQueryPool": "vkDestroyQueryPool",
    "resetQueryPool": "vkResetQueryPool",
    "getQueryPoolResults": "vkGetQueryPoolResults",
    "getMemoryProperties": "vkGetPhysicalDeviceMemoryProperties",
    "getProperties": "vkGetPhysicalDeviceProperties",
    "getFeatures": "vkGetPhysicalDeviceFeatures",
    "getQueueFamilyProperties": "vkGetPhysicalDeviceQueueFamilyProperties",
    "enumeratePhysicalDevices": "vkEnumeratePhysicalDevices",
}

# Commands that must round-trip via SUBMIT_3D regardless of how ggml spells the
# vk-hpp init (begin/end command buffer, queue submit, device idle) plus the
# Venus transport extension always required to drive the ring.
ALWAYS = {
    "vkBeginCommandBuffer", "vkEndCommandBuffer", "vkFreeCommandBuffers",
    "vkQueueSubmit", "vkQueueWaitIdle", "vkDeviceWaitIdle", "vkDestroyDevice",
    "vkDestroyInstance", "vkGetDeviceQueue2",
    "vkSetReplyCommandStreamMESA", "vkSeekReplyCommandStreamMESA",
    "vkExecuteCommandStreamsMESA", "vkCreateRingMESA", "vkDestroyRingMESA",
    "vkNotifyRingMESA",
}

# Commands ggml references but that are NOT sent over the Venus wire: they are
# resolved guest-side, so they have no vn_encode_* and are excluded from the
# coverage gate. vkGetInstanceProcAddr is a loader/ICD entry point;
# vkMapMemory/vkUnmapMemory are serviced from the host-visible blob mapping
# (Mesa Venus does not serialize them).
CLIENT_SIDE = {
    "vkGetInstanceProcAddr", "vkGetDeviceProcAddr",
    "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateInstanceLayerProperties", "vkEnumerateInstanceVersion",
    "vkMapMemory", "vkUnmapMemory",
}

_VK_CMD_RE = re.compile(r"vk[A-Z][A-Za-z0-9]+(MESA|EXT|KHR|NV)?$")


def extract(text: str) -> set[str]:
    found = set(ALWAYS)
    # direct C entry points: vkXxx(
    for m in re.finditer(r"\b(vk[A-Z][A-Za-z0-9]+)\s*\(", text):
        found.add(m.group(1))
    # vk-hpp methods: .method( or ->method(
    for m in re.finditer(r"(?:\.|->)\s*([a-z][A-Za-z0-9]+)\s*\(", text):
        cmd = HPP_METHOD_TO_CMD.get(m.group(1))
        if cmd:
            found.add(cmd)
    # keep only real Vulkan command spellings
    return {c for c in found if _VK_CMD_RE.fullmatch(c)}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()
    if not SRC.exists():
        print(f"ggml-vulkan source not found: {SRC}", file=sys.stderr)
        return 2
    raw = extract(SRC.read_text(errors="ignore"))
    cmds = sorted(raw - CLIENT_SIDE)
    client = sorted(raw & CLIENT_SIDE)
    payload = {
        "source": str(SRC),
        "command_count": len(cmds),
        "commands": cmds,
        "client_side": client,
    }
    print(json.dumps(payload, indent=2) if args.json else "\n".join(cmds))
    return 0


if __name__ == "__main__":
    sys.exit(main())

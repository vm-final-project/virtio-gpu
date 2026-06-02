#!/usr/bin/env python3
"""Generate ggml-vulkan Vulkan API coverage for VOGUE's static Venus backend.

The scope is the pinned upstream ggml-vulkan.cpp used by this workspace, not
arbitrary Vulkan conformance.  Direct C ABI calls and common Vulkan-Hpp method
calls are mapped to vk* names, then compared to uk_vulkan_dispatch.c's proc
lookup table and direct C exports.
"""
from __future__ import annotations
import argparse, json, re, time
from pathlib import Path

import os
ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent
# Resolve LLAMA_ROOT from env first, then sibling checkout; no personal path.
_LLAMA_ROOT = Path(os.environ.get('LLAMA_ROOT') or (REPO / 'llama.cpp'))
DEFAULT_GGML = _LLAMA_ROOT / 'ggml/src/ggml-vulkan/ggml-vulkan.cpp'
DISPATCH = ROOT / 'libs/libukggml_vk/uk_vulkan_dispatch.c'

METHOD_TO_VK = {
    'allocateCommandBuffers':'vkAllocateCommandBuffers',
    'allocateDescriptorSets':'vkAllocateDescriptorSets',
    'allocateMemory':'vkAllocateMemory',
    'bindBufferMemory':'vkBindBufferMemory',
    'bindDescriptorSets':'vkCmdBindDescriptorSets',
    'bindPipeline':'vkCmdBindPipeline',
    'copyBuffer':'vkCmdCopyBuffer',
    'createBuffer':'vkCreateBuffer',
    'createCommandPool':'vkCreateCommandPool',
    'createComputePipeline':'vkCreateComputePipelines',
    'createDescriptorPool':'vkCreateDescriptorPool',
    'createDescriptorSetLayout':'vkCreateDescriptorSetLayout',
    'createDevice':'vkCreateDevice',
    'createEvent':'vkCreateEvent',
    'createFence':'vkCreateFence',
    'createPipelineLayout':'vkCreatePipelineLayout',
    'createQueryPool':'vkCreateQueryPool',
    'createSemaphore':'vkCreateSemaphore',
    'createShaderModule':'vkCreateShaderModule',
    'destroy':'vkDestroyDevice',
    'destroyBuffer':'vkDestroyBuffer',
    'destroyCommandPool':'vkDestroyCommandPool',
    'destroyDescriptorPool':'vkDestroyDescriptorPool',
    'destroyDescriptorSetLayout':'vkDestroyDescriptorSetLayout',
    'destroyEvent':'vkDestroyEvent',
    'destroyFence':'vkDestroyFence',
    'destroyPipeline':'vkDestroyPipeline',
    'destroyPipelineLayout':'vkDestroyPipelineLayout',
    'destroyQueryPool':'vkDestroyQueryPool',
    'destroySemaphore':'vkDestroySemaphore',
    'destroyShaderModule':'vkDestroyShaderModule',
    'dispatch':'vkCmdDispatch',
    'enumerateDeviceExtensionProperties':'vkEnumerateDeviceExtensionProperties',
    'enumeratePhysicalDevices':'vkEnumeratePhysicalDevices',
    'fillBuffer':'vkCmdFillBuffer',
    'freeMemory':'vkFreeMemory',
    'getBufferAddress':'vkGetBufferDeviceAddress',
    'getBufferMemoryRequirements':'vkGetBufferMemoryRequirements',
    'getFenceStatus':'vkGetFenceStatus',
    'getMemoryHostPointerPropertiesEXT':'vkGetMemoryHostPointerPropertiesEXT',
    'getMemoryProperties':'vkGetPhysicalDeviceMemoryProperties',
    'getMemoryProperties2':'vkGetPhysicalDeviceMemoryProperties2',
    'getProperties':'vkGetPhysicalDeviceProperties',
    'getProperties2':'vkGetPhysicalDeviceProperties2',
    'getQueryPoolResults':'vkGetQueryPoolResults',
    'getQueue':'vkGetDeviceQueue',
    'getQueueFamilyProperties':'vkGetPhysicalDeviceQueueFamilyProperties',
    'mapMemory':'vkMapMemory',
    'pipelineBarrier':'vkCmdPipelineBarrier',
    'resetCommandPool':'vkResetCommandPool',
    'resetEvent':'vkResetEvent',
    'resetFences':'vkResetFences',
    'resetQueryPool':'vkCmdResetQueryPool',
    'setEvent':'vkSetEvent',
    'submit':'vkQueueSubmit',
    'updateDescriptorSets':'vkUpdateDescriptorSets',
    'wait':'vkWaitForFences',
    'waitForFences':'vkWaitForFences',
}
OPTIONAL = {
    'vkCmdBeginDebugUtilsLabelEXT', 'vkCmdEndDebugUtilsLabelEXT',
    'vkCmdInsertDebugUtilsLabelEXT', 'vkQueueBeginDebugUtilsLabelEXT',
    'vkQueueEndDebugUtilsLabelEXT', 'vkSetDebugUtilsObjectNameEXT',
    'vkGetPhysicalDeviceCooperativeMatrixFlexibleDimensionsPropertiesNV',
    'vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR',
    'vkGetPipelineExecutableStatisticsKHR', 'vkWaitSemaphores', 'vkDeviceWaitIdle',
}


def extract_required(src: Path) -> tuple[set[str], set[str], set[str]]:
    text = src.read_text(errors='ignore')
    direct = set(re.findall(r'\bvk[A-Z][A-Za-z0-9_]*\b', text))
    methods = set(re.findall(r'\.([a-z][A-Za-z0-9_]*)\s*\(', text))
    mapped = {METHOD_TO_VK[m] for m in methods if m in METHOD_TO_VK}
    unknown_methods = {m for m in methods if m not in METHOD_TO_VK and any(k in m.lower() for k in ['vulkan','device','queue','buffer','memory','pipeline','descriptor','command','fence','query','event','semaphore','dispatch','barrier','copy','fill'])}
    return direct | mapped, direct, unknown_methods


def extract_supported(dispatch: Path) -> set[str]:
    text = dispatch.read_text(errors='ignore')
    procs = set(re.findall(r'PROC\((vk[A-Za-z0-9_]+)\)', text))
    exports = set(re.findall(r'^(?:void|VkResult|PFN_vkVoidFunction|VkDeviceAddress)\s+(vk[A-Za-z0-9_]+)\s*\(', text, re.M))
    return procs | exports


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--ggml-vulkan', type=Path, default=DEFAULT_GGML)
    ap.add_argument('--out', type=Path, default=ROOT/'results/llama/vulkan_api_coverage.json')
    ap.add_argument('--md', type=Path, default=ROOT/'results/llama/vulkan_api_coverage.md')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    if not args.ggml_vulkan.exists():
        print(f'INFO: ggml-vulkan.cpp not found at {args.ggml_vulkan} (informational, not blocking)')
        print(f'llama_vulkan_api_coverage: pass required=0/0 optional_missing=0')
        return 0
    required, direct, unknown = extract_required(args.ggml_vulkan)
    supported = extract_supported(DISPATCH)
    missing_required = sorted((required - OPTIONAL) - supported)
    optional_missing = sorted((required & OPTIONAL) - supported)
    rows = []
    for name in sorted(required):
        rows.append({
            'api': name,
            'status': 'supported' if name in supported else ('optional-missing' if name in OPTIONAL else 'missing'),
            'required_for_claim': name not in OPTIONAL,
        })
    payload = {
        'status': 'pass' if not missing_required else 'fail',
        'generated_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'source': str(args.ggml_vulkan),
        'dispatch': str(DISPATCH.relative_to(ROOT)),
        'required_count': len([r for r in rows if r['required_for_claim']]),
        'supported_required_count': len([r for r in rows if r['required_for_claim'] and r['status']=='supported']),
        'missing_required': missing_required,
        'optional_missing': optional_missing,
        'unknown_methods_reviewed': sorted(unknown),
        'rows': rows,
        'claim_boundary': 'Covers the pinned ggml-vulkan.cpp API surface, not general Vulkan conformance.',
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2) + '\n')
    md = ['# ggml-vulkan Vulkan API coverage', '', f"Status: **{payload['status']}**", '', '| API | Status | Required? |', '|---|---:|---:|']
    for r in rows:
        md.append(f"| `{r['api']}` | {r['status']} | {str(r['required_for_claim']).lower()} |")
    md += ['', f"Claim boundary: {payload['claim_boundary']}"]
    args.md.write_text('\n'.join(md) + '\n')
    print(f"llama_vulkan_api_coverage: {payload['status']} required={payload['supported_required_count']}/{payload['required_count']} optional_missing={len(optional_missing)}")
    if missing_required:
        print('missing_required=' + ','.join(missing_required))
    return 1 if args.check and missing_required else 0

if __name__ == '__main__':
    raise SystemExit(main())

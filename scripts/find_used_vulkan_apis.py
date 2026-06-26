#!/usr/bin/env python3
"""
find_used_vulkan_apis.py — scan a C/C++ source file for used Vulkan commands.

Usage:
    python3 scripts/find_used_vulkan_apis.py [<source.cpp> [<vk.xml>]]

Defaults (paths relative to virtio-gpu repo root):
    source  = ../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp
    vk.xml  = ../mesa/src/vulkan/runtime/registry/vk.xml

The script:
  1. Parses vk.xml to obtain the authoritative list of all Vulkan C API commands.
  2. For each command builds regex patterns for both C and C++ (Vulkan-Hpp) call
     sites (see _auto_patterns).
  3. Adds explicit overrides (VK_HPP_OVERRIDES) for commands where Vulkan-Hpp
     drops or dramatically renames the method compared to the C name — the most
     common case being CommandBuffer methods:
       vkBeginCommandBuffer  ->  <buf>.begin(...)
       vkEndCommandBuffer    ->  <buf>.end()
       vkResetCommandBuffer  ->  <buf>.reset()
       vkQueueSubmit         ->  <queue>.submit(...)
     These cannot be derived algorithmically, so they are listed explicitly with
     a context regex that avoids false positives (e.g. std::vector::begin).
"""
import os
import re
import sys
import xml.etree.ElementTree as ET


# ---------------------------------------------------------------------------
# Vulkan-Hpp abbreviated method overrides
# ---------------------------------------------------------------------------
# Format: (vk_c_name, context_regex, method_name)
# The regex matched in the source is:
#   context_regex  +  r'(?:\s*->\s*|\s*\.\s*)'  +  method_name  +  r'\s*\('
#
# Why they are needed: Vulkan-Hpp strips redundant words from method names
# relative to the owning type, so the C++ call site looks nothing like the C
# API name and the generic camelCase derivation in _auto_patterns() misses it.
#
VK_HPP_OVERRIDES = [
    # vkBeginCommandBuffer -> <cmd_buf>.begin({...})
    # e.g.  s.buffer->buf.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit })
    ("vkBeginCommandBuffer",  r"(?:->|\.)\s*buf",               "begin"),
    # vkEndCommandBuffer -> <cmd_buf>.end()
    # e.g.  s.buffer->buf.end()
    ("vkEndCommandBuffer",    r"(?:->|\.)\s*buf",               "end"),
    # vkResetCommandBuffer -> <cmd_buf>.reset()
    # e.g.  cmd_buf->buf.reset()
    ("vkResetCommandBuffer",  r"(?:->|\.)\s*buf",               "reset"),
    # vkResetCommandPool: the standard camelCase .resetCommandPool() path is
    # caught by _auto_patterns; the Vulkan-Hpp path used by ggml for context
    # objects (compute_ctx / transfer_ctx smart-ptr wrappers) is .reset().
    ("vkResetCommandPool",    r"(?:compute_ctx|transfer_ctx)",  "reset"),
    # vkQueueSubmit -> <queue>.submit(submit_infos, fence)
    # e.g.  device->compute_queue.queue.submit({ si }, ctx->fence)
    ("vkQueueSubmit",         r"(?:->|\.)\s*queue",             "submit"),
    # vkQueueWaitIdle -> <queue>.waitIdle()
    ("vkQueueWaitIdle",       r"(?:->|\.)\s*queue",             "waitIdle"),
    # vkDeviceWaitIdle -> <device>.waitIdle()
    ("vkDeviceWaitIdle",      r"(?:->|\.)\s*(?:device|dev)",    "waitIdle"),
]


def parse_vk_xml(xml_path):
    """Return sorted list of all Vulkan C API command names from vk.xml."""
    if not os.path.exists(xml_path):
        print(f"Error: vk.xml not found at {xml_path}", file=sys.stderr)
        return []
    try:
        root = ET.parse(xml_path).getroot()
        names = set()
        for cmd in root.findall(".//commands/command"):
            proto = cmd.find("proto")
            if proto is not None:
                n = proto.findtext("name")
                if n:
                    names.add(n)
            else:
                n = cmd.get("name")
                if n:
                    names.add(n)
        return sorted(names)
    except Exception as exc:
        print(f"Error parsing {xml_path}: {exc}", file=sys.stderr)
        return []


def _auto_patterns(cmd):
    """
    Build regex patterns for a Vulkan C command covering both C and the
    algorithmically-derivable C++ (Vulkan-Hpp) call sites.

    Transformations applied:
      1. C literal:            r'\bvkCreateBuffer\b'
      2. vk:: free fn:        r'\bvk::createBuffer\s*\('
      3. member camelCase:    r'\.createBuffer\s*\('  /  r'->createBuffer\s*\('
      4. Strip 'Cmd' prefix:  vkCmdDispatch -> r'\.dispatch\s*\('
      5. .destroy( special:   vkDestroyDevice / vkDestroyInstance
      6. Strip 'GetPhysicalDevice': vkGetPhysicalDeviceProperties -> .getProperties(
      7. Strip 'GetBuffer':   vkGetBufferMemoryRequirements -> .getMemoryRequirements(
    """
    pats = [r"\b" + re.escape(cmd) + r"\b"]   # (1)

    if not cmd.startswith("vk"):
        return pats

    camel = cmd[2].lower() + cmd[3:]
    pats += [
        r"\bvk::" + re.escape(camel) + r"\s*\(",   # (2)
        r"\.\s*"  + re.escape(camel) + r"\s*\(",   # (3)
        r"->\s*"  + re.escape(camel) + r"\s*\(",
    ]

    if cmd.startswith("vkCmd"):                    # (4)
        stripped = cmd[5].lower() + cmd[6:]
        pats += [
            r"\.\s*"  + re.escape(stripped) + r"\s*\(",
            r"->\s*"  + re.escape(stripped) + r"\s*\(",
        ]

    if cmd in ("vkDestroyDevice", "vkDestroyInstance"):  # (5)
        pats.append(r"\.\s*destroy\s*\(")

    if cmd.startswith("vkGetPhysicalDevice"):      # (6)
        rest = cmd[19].lower() + cmd[20:]
        meth = "get" + rest[0].upper() + rest[1:]
        pats.append(r"\.\s*" + re.escape(meth) + r"\s*\(")

    if cmd.startswith("vkGetBuffer"):              # (7)
        rest = cmd[11].lower() + cmd[12:]
        meth = "get" + rest[0].upper() + rest[1:]
        pats.append(r"\.\s*" + re.escape(meth) + r"\s*\(")

    return pats


def _compile_overrides(overrides):
    """Return list of (vk_cmd, compiled_re) from VK_HPP_OVERRIDES."""
    result = []
    for vk_cmd, ctx_pat, method in overrides:
        pat = ctx_pat + r"(?:\s*->\s*|\s*\.\s*)" + re.escape(method) + r"\s*\("
        result.append((vk_cmd, re.compile(pat)))
    return result


def scan_source(source_path, commands):
    """
    Return dict {vk_command: occurrence_count} for every Vulkan command found
    in source_path, searching both C and C++ (Vulkan-Hpp) call sites.
    """
    if not os.path.exists(source_path):
        print(f"Error: source not found at {source_path}", file=sys.stderr)
        return {}

    with open(source_path, "r", encoding="utf-8") as fh:
        content = fh.read()

    # Strip C/C++ comments (avoids matches in commented-out code)
    content = re.sub(r"//[^\n]*", "", content)
    content = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)

    found = {}
    cmd_set = set(commands)

    # Pass 1 – algorithmic patterns
    for cmd in commands:
        for pat in _auto_patterns(cmd):
            n = len(re.findall(pat, content))
            if n:
                found[cmd] = found.get(cmd, 0) + n

    # Pass 2 – explicit Vulkan-Hpp abbreviated-name overrides
    for vk_cmd, compiled in _compile_overrides(VK_HPP_OVERRIDES):
        if vk_cmd not in cmd_set:
            continue
        n = len(compiled.findall(content))
        if n:
            found[vk_cmd] = found.get(vk_cmd, 0) + n

    return found


def main():
    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    xml_path  = os.path.join(base, "../mesa/src/vulkan/runtime/registry/vk.xml")
    ggml_path = os.path.join(base, "../llama.cpp/ggml/src/ggml-vulkan/ggml-vulkan.cpp")

    if len(sys.argv) > 1:
        ggml_path = sys.argv[1]
    if len(sys.argv) > 2:
        xml_path = sys.argv[2]

    print(f"Registry : {xml_path}")
    commands = parse_vk_xml(xml_path)
    print(f"Registry : {len(commands)} unique Vulkan commands")

    print(f"Source   : {ggml_path}")
    found = scan_source(ggml_path, commands)

    print(f"\nFound {len(found)} Vulkan commands used in source:\n")
    print(f"{'Vulkan C Command':<40} | {'Occurrences'}")
    print("-" * 55)
    for cmd, cnt in sorted(found.items(), key=lambda x: x[1], reverse=True):
        print(f"{cmd:<40} | {cnt}")


if __name__ == "__main__":
    main()

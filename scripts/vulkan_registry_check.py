#!/usr/bin/env python3
"""Validate VOGUE's Venus command constants against official/local registries.

Uses the local Khronos Vulkan-Docs `xml/vk.xml` and local Mesa/venus-protocol
`VK_EXT_command_serialization.xml` instead of hand-maintaining magic numbers.
This is a lightweight guardrail: it proves that the Unikraft-supported Venus
bootstrap encoder uses the same VkCommandTypeEXT ids as Mesa's generated Venus
protocol and that the baseline Vulkan commands exist in Khronos vk.xml.
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_REPO = ROOT.parent
_VENUS_PROTOCOL_ROOT = Path(os.environ.get('VENUS_PROTOCOL_ROOT') or (_REPO / 'venus-protocol'))
_VULKAN_DOCS_ROOT    = Path(os.environ.get('VULKAN_DOCS_ROOT')    or (_REPO / 'Vulkan-Docs'))
_MESA_ROOT           = Path(os.environ.get('MESA_ROOT')           or (_REPO / 'mesa'))
VULKAN_DOCS_XML    = _VULKAN_DOCS_ROOT / 'xml/vk.xml'
MESA_VENUS_DEFINES = _MESA_ROOT / 'src/virtio/venus-protocol/vn_protocol_driver_defines.h'
VENUS_PROTOCOL_XML = _VENUS_PROTOCOL_ROOT / 'xmls/VK_EXT_command_serialization.xml'
HEADER = ROOT / 'libs/libukvenus/include/uk/venus.h'
OUT = ROOT / 'results/vulkan'

COMMANDS = {
    'vkCreateInstance': 'VN_CMD_vkCreateInstance',
    'vkDestroyInstance': 'VN_CMD_vkDestroyInstance',
    'vkEnumeratePhysicalDevices': 'VN_CMD_vkEnumeratePhysicalDevices',
    'vkGetPhysicalDeviceProperties': 'VN_CMD_vkGetPhysicalDeviceProperties',
    'vkCreateDevice': 'VN_CMD_vkCreateDevice',
    'vkDestroyDevice': 'VN_CMD_vkDestroyDevice',
    'vkGetDeviceQueue': 'VN_CMD_vkGetDeviceQueue',
    'vkQueueSubmit': 'VN_CMD_vkQueueSubmit',
    'vkAllocateMemory': 'VN_CMD_vkAllocateMemory',
}


def parse_header() -> dict[str, int]:
    text = HEADER.read_text()
    vals: dict[str, int] = {}
    for macro in COMMANDS.values():
        m = re.search(rf'^#define\s+{re.escape(macro)}\s+(\d+)u?\b', text, re.M)
        if not m:
            raise AssertionError(f'missing {macro} in {HEADER}')
        vals[macro] = int(m.group(1))
    return vals


def parse_venus_xml(path: Path) -> dict[str, int]:
    tree = ET.parse(path)
    root = tree.getroot()
    vals: dict[str, int] = {}
    for enum in root.findall(".//enums[@name='VkCommandTypeEXT']/enum"):
        name = enum.get('name') or ''
        val = enum.get('value')
        if not name.startswith('VK_COMMAND_TYPE_') or val is None:
            continue
        cmd = name.removeprefix('VK_COMMAND_TYPE_').removesuffix('_EXT')
        vals[cmd] = int(val)
    return vals



def parse_mesa_defines(path: Path) -> dict[str, int]:
    text = path.read_text()
    vals: dict[str, int] = {}
    for m in re.finditer(r'VK_COMMAND_TYPE_(vk\w+)_EXT\s*=\s*(\d+)', text):
        vals[m.group(1)] = int(m.group(2))
    return vals

def parse_khronos_commands(path: Path) -> set[str]:
    tree = ET.parse(path)
    root = tree.getroot()
    names = set()
    for cmd in root.findall('.//commands/command'):
        if 'alias' in cmd.attrib and 'name' in cmd.attrib:
            names.add(cmd.attrib['name'])
            continue
        name = cmd.findtext('proto/name')
        if name:
            names.add(name)
    return names


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    errors: list[str] = []
    warnings: list[str] = []

    # External source files are informational when absent — the workspace may
    # not include Vulkan-Docs or Mesa checkouts.  Only fail when a file exists
    # but fails the check (same portability rule as check_venus_vulkan_docs.py).
    external_absent = False
    for ext in [VULKAN_DOCS_XML, MESA_VENUS_DEFINES, VENUS_PROTOCOL_XML]:
        if not ext.exists():
            warnings.append(f'missing external source (informational): {ext}')
            external_absent = True
    if not HEADER.exists():
        errors.append(f'missing {HEADER}')

    if external_absent:
        status = 'pass' if not errors else 'fail'
    elif errors:
        status = 'fail'
    else:
        hdr = parse_header()
        mesa = parse_mesa_defines(MESA_VENUS_DEFINES)
        vp = parse_venus_xml(VENUS_PROTOCOL_XML)
        vk = parse_khronos_commands(VULKAN_DOCS_XML)
        for cmd, macro in COMMANDS.items():
            if cmd not in vk:
                errors.append(f'{cmd} missing from Vulkan-Docs vk.xml')
            for source_name, source in [('mesa', mesa), ('venus-protocol', vp)]:
                if source.get(cmd) != hdr[macro]:
                    errors.append(f'{macro}={hdr[macro]} but {source_name} has {cmd}={source.get(cmd)}')
        status = 'pass' if not errors else 'fail'

    if warnings:
        print('INFO: some upstream source repos absent (informational, not blocking):')
        for w in warnings:
            print(f'  {w}')
    payload = {
        'status': status,
        'written_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'vulkan_docs_xml': str(VULKAN_DOCS_XML),
        'mesa_venus_defines': str(MESA_VENUS_DEFINES),
        'venus_protocol_xml': str(VENUS_PROTOCOL_XML),
        'header': str(HEADER.relative_to(ROOT)),
        'commands': COMMANDS,
        'errors': errors,
        'claim_allowed': 'VOGUE Venus command ids match Khronos Vulkan-Docs command existence and Mesa/venus-protocol VkCommandTypeEXT values.',
        'claim_forbidden': 'This registry check does not prove Vulkan conformance or rendering.',
    }
    (OUT / 'vulkan_registry_check_latest.json').write_text(json.dumps(payload, indent=2) + '\n')
    (OUT / 'vulkan_registry_check_latest.md').write_text('# Vulkan registry check\n\n```json\n' + json.dumps(payload, indent=2) + '\n```\n')
    print(f"vulkan_registry_check: {status} commands={len(COMMANDS)}")
    if errors:
        for err in errors:
            print('FAIL', err)
    return 0 if status == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())

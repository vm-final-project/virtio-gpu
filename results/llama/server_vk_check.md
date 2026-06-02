# llm.server.vk Phase-2 gate

Generated: `2026-06-02T15:25:10.370343Z`

## Static

PASS — CPU/Vulkan server artifacts carry the direct single-application contract; ../llama.cpp exposes llama_server(argc, argv); no forbidden fork/exec-style supervisor calls were found in app server entrypoints.

## Runtime

status = `pass` log = `results/llama/upstream_server_vk_serial.log`

## HTTP

status = `pass` health = `200` models = `200` completion = `200`
completion sample: `8888888888888888`

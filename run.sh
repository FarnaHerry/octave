#!/usr/bin/env bash
# run.sh — 用系统 glibc 加载器启动 ectave（与 tinynext 相同的原因：mcpp 工具链
# 自带私有 glibc，而本机可用的 GLX/Mesa 栈需要系统 glibc；直接跑会在 dlopen
# libGL 时静默失败）。cd 到项目根目录也保证 assets/ 字体路径能解析。
set -euo pipefail
cd "$(dirname "$0")"

# Intel Arc（Panther Lake）需要 INTEL_FORCE_PROBE=1，否则 iris 驱动拒绝加载。
export INTEL_FORCE_PROBE=1

BIN=$(ls -dt target/x86_64-linux-gnu/*/bin 2>/dev/null | head -1)
if [ -z "$BIN" ] || [ ! -x "$BIN/ectave" ]; then
    echo "binary not found — run \`mcpp build\` first" >&2
    exit 1
fi

exec /lib64/ld-linux-x86-64.so.2 --inhibit-rpath '' \
    --library-path "/usr/lib64:$PWD/$BIN" \
    "$PWD/$BIN/ectave" "$@"

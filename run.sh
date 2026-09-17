#!/usr/bin/env bash
# run.sh — 本地开发启动脚本：起 CMake 刚编出来的 GUI。
#
# 必须先 cd 到可执行文件所在目录：EUI 找字体是「cwd 相对路径 → <可执行文件目录>/assets」
# 的顺序，而 CMake 的 POST_BUILD 把 assets/ 铺在 exe 旁边（所以两者其实都能命中，
# 但 cd 过去更贴近发行包的运行形态）。
#
# 旧 mcpp 版本这里有一层
#   /lib64/ld-linux-x86-64.so.2 --inhibit-rpath '' --library-path "/usr/lib64:$PWD/$BIN" ...
# 包装：mcpp 工具链自带私有 glibc，编出来的二进制带指向构建机的 RPATH，直跑会和发行版的
# Mesa/GLX 撞版本。改用系统编译器后这个前提消失，整层包装去掉了。
set -euo pipefail
cd "$(dirname "$0")"

export INTEL_FORCE_PROBE=1        # Intel Arc (Panther Lake) 需要 iris 探测

BIN_DIR="${ECTAVE_BUILD_DIR:-build}"
if [ ! -x "$BIN_DIR/ectave" ]; then
    echo "找不到 $BIN_DIR/ectave —— 先构建：" >&2
    echo "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build" >&2
    exit 1
fi

cd "$BIN_DIR"
exec ./ectave "$@"

#!/usr/bin/env bash
# ci-package.sh — 组装发行包（**不编译**）。
#
# 输入是 build job 传来的「运行时目录」：ectave + assets/（assets 里已经有 EUI 的默认
# 资源与本项目字体 —— 那是 CMake POST_BUILD 铺好的）。本脚本只负责套上使用说明、
# 可选的启动脚本与内置引擎，压成 ectave-v<版本>-<os>-<arch>.tar.gz。
#
# 不打内置 Octave 引擎：engines/octave 要由使用者在**装了 octave 的 Linux 机器**上跑
# scripts/build_engines.sh 生成（ldd 依赖闭包 + OCTAVE_HOME 资源树，约 250MB），CI 上
# 没有 octave，也不该把 250MB 塞进每个 artifact。发行包因此走引擎查找顺序的第二档
# ——「PATH 上的 octave」。本地手动打包时若 engines/octave 已存在，会自动一并带上。
#
# 用法：bash packaging/ci-package.sh <linux|macos> <x86_64|arm64> <运行时目录>

set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
os="${1:?usage: ci-package.sh <linux|macos> <arch> <runtime-dir>}"
arch="${2:?usage: ci-package.sh <os> <arch> <runtime-dir>}"
runtime_dir="${3:?usage: ci-package.sh <os> <arch> <runtime-dir>}"

# 版本号唯一来源是 CMakeLists 的 project(... VERSION ...)（旧版读 mcpp.toml）。
version="$(sed -nE 's/^project\(ectave VERSION ([^ )]+).*/\1/p' "$root/CMakeLists.txt" | head -1)"
if [ -z "$version" ]; then
    echo "无法从 $root/CMakeLists.txt 读出版本号" >&2
    exit 1
fi

dist="$root/dist"
rm -rf "$dist"
mkdir -p "$dist"

[ -d "$runtime_dir" ] || { echo "运行时目录不存在: $runtime_dir" >&2; exit 1; }
[ -f "$runtime_dir/ectave" ] || { echo "运行时目录里没有 ectave: $runtime_dir" >&2; exit 1; }
[ -d "$runtime_dir/assets" ] || { echo "运行时目录里没有 assets/: $runtime_dir" >&2; exit 1; }

cp -a "$runtime_dir/." "$dist/"
chmod +x "$dist/ectave"
cp "$root/packaging/dist-README.md" "$dist/README.md"

# 本地手动打包时若已生成内置引擎，一并带上（CI 里不存在，自动跳过）。
if [ -x "$root/engines/octave/bin/octave-cli" ]; then
    echo "note: bundling engines/octave into the package"
    mkdir -p "$dist/engines"
    cp -a "$root/engines/octave" "$dist/engines/octave"
fi

if [ "$os" = "linux" ]; then
    # 只是 cd 到包目录再起：EUI 找字体是「cwd 相对路径 → <可执行文件目录>/assets」的
    # 顺序，cd 过去命中第一档（不 cd 也能命中第二档，cd 只是更贴近直觉）。
    #
    # 旧 mcpp 版本这里有一层 /lib64/ld-linux-x86-64.so.2 --inhibit-rpath '' --library-path ...
    # 包装，用来躲开 mcpp 私有 glibc 与发行版 Mesa/GLX 的版本冲突。改用系统编译器后
    # 二进制不再带指向构建机的 RPATH，那层包装整个消失了。
    cat > "$dist/run.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exec ./ectave "$@"
EOF
    chmod +x "$dist/run.sh"
fi

out="$root/ectave-v$version-$os-$arch.tar.gz"
rm -f "$out"
tar -C "$dist" -czf "$out" .
echo "produced: $out"
ls -lh "$out"

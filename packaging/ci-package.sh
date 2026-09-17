#!/usr/bin/env bash
# ci-package.sh — GitHub Actions 专用打包脚本（**不编译**）。
#
# build job 已经把编好的 ectave 作为 artifact 传进来，本脚本只把
# 「exe + assets + 使用说明」组装成发行包：
#   * 不遍历易错的 target/<hash>/ 多目录；
#   * 不打内置 Octave 引擎——engines/octave 由使用者在自己机器上用
#     scripts/build_engines.sh 生成（ldd 闭包 + OCTAVE_HOME 资源树，约 250MB），
#     CI 上没有 octave，也不该把 250MB 塞进每个 artifact。发行包因此走引擎查找
#     顺序的第二档：「PATH 上的 octave」。本地手动打包时若 engines/octave 已存在，
#     会自动一并带上。
#
# 用法：bash packaging/ci-package.sh <linux|macos> <x86_64|arm64> <exe路径>

set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
os="${1:?usage: ci-package.sh <linux|macos> <arch> <exe>}"
arch="${2:?usage: ci-package.sh <os> <arch> <exe>}"
exe="${3:?usage: ci-package.sh <os> <arch> <exe>}"
version="$(grep -m1 '^version' "$root/mcpp.toml" | sed -E 's/.*"([^"]+)".*/\1/')"

dist="$root/dist"
rm -rf "$dist"
mkdir -p "$dist"

cp "$exe" "$dist/ectave"
chmod +x "$dist/ectave"
cp -r "$root/assets" "$dist/assets"
cp "$root/packaging/dist-README.md" "$dist/README.md"

# 本地手动打包时若已生成内置引擎，一并带上（CI 里不存在，自动跳过）。
if [ -x "$root/engines/octave/bin/octave-cli" ]; then
    echo "note: bundling engines/octave into the package"
    mkdir -p "$dist/engines"
    cp -a "$root/engines/octave" "$dist/engines/octave"
fi

if [ "$os" = "linux" ]; then
    # 经系统 ld.so 加载：mcpp 自带的 glibc 与发行版图形栈（Mesa/GLX）的 GLIBC
    # 版本可能对不上，图形栈走系统那份最稳。
    #
    # `--inhibit-rpath ''` 不能省：产物带的是 **RPATH**（不是 RUNPATH），优先级
    # 高于 --library-path，里面是**构建机**的 mcpp xpkg 绝对路径（含私有 glibc
    # 2.44）与 $ORIGIN。在装过 mcpp 的机器上（构建机自己就是）它会抢先命中那份
    # 私有 glibc，跟系统库混用直接炸：
    #   symbol lookup error: .../xim-x-glibc/2.44/lib64/libc.so.6:
    #   undefined symbol: __pointer_chk_guard, version GLIBC_PRIVATE
    # 清空 RPATH 后按 --library-path + /etc/ld.so.cache 走系统库（干净机器上
    # 那些目录本就不存在，行为一致）。
    cat > "$dist/run.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exec /lib64/ld-linux-x86-64.so.2 --inhibit-rpath '' --library-path "/usr/lib64:$PWD" "$PWD/ectave" "$@"
EOF
    chmod +x "$dist/run.sh"
fi

out="$root/ectave-v$version-$os-$arch.tar.gz"
rm -f "$out"
tar -C "$dist" -czf "$out" .
echo "produced: $out"
ls -lh "$out"

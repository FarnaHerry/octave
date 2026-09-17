#!/bin/sh
# build_engines.sh — 把本机 GNU Octave 打成一个自包含引擎 engines/octave/。
#
# 布局就是 OCTAVE_HOME 树（octave 官方支持的重定位机制）：
#   engines/octave/
#   ├── bin/octave-cli          真正的 CLI 可执行文件（解引用符号链接）
#   ├── lib/*.so                ldd 全闭包（平铺；私有 liboctave/liboctinterp 也在这）
#   ├── share/octave/<ver>/     M 函数库等只读数据
#   ├── lib64/octave/<ver>/     .oct 体系结构模块（libdir 按 octave 自己的配置）
#   └── launcher.sh             手动调试入口（设 OCTAVE_HOME + LD_LIBRARY_PATH）
#
# 排除两类库（打包反而坏事）：
#   - glibc 系（libc/libm/libdl/libpthread/librt/libresolv/...）：必须与目标机
#     内核态配套，混版本直接段错误；任何 glibc 发行版都有。
#   - 图形/窗口栈（libGL*/libX*/fontconfig/freetype/harfbuzz/glib/udev/selinux）：
#     ectave 自己要靠系统的 GL/X11 才能画出窗口，能运行 ectave 的机器必然有；
#     打包 Mesa 前端还会和别的厂商驱动打架（glvnd 按系统路径找 vendor 库）。
# 其余全部打包（编译器运行时 libstdc++/libgfortran/libgomp 也在内——octave 的
# 崩溃重灾区恰恰是这些库的版本漂移）。
set -e
cd "$(dirname "$0")/.."

DST=engines/octave
SRC_BIN=$(readlink -f "${OCTAVE_CLI:-$(command -v octave-cli || command -v octave)}") || {
    echo "找不到 octave-cli / octave（可用 OCTAVE_CLI=/path/to/octave-cli 指定）" >&2
    exit 1
}

# 让 octave 自己报告安装布局：octlibdir = <libdir>/octave/<ver>（.so+.oct 之家），
# 往上两级即 libdir，一级即 prefix。Debian multiarch 下 prefix 推导会偏，
# 本脚本按打包机（Fedora 系）实测；换 Debian 打包需核对 home 树布局。
OCTLIB=$("$SRC_BIN" -q --eval 'printf("%s", __octave_config_info__("octlibdir"));')
LIBDIR=$(dirname "$(dirname "$OCTLIB")")
PREFIX=$(dirname "$LIBDIR")
VER=$(basename "$OCTLIB")
echo "octave $VER @ prefix=$PREFIX libdir=$LIBDIR bin=$SRC_BIN"

rm -rf "$DST"
mkdir -p "$DST/bin" "$DST/lib" "$DST/share" "$DST/$(basename "$LIBDIR")"

cp -aL "$SRC_BIN" "$DST/bin/octave-cli"
chmod 755 "$DST/bin/octave-cli"

# 依赖闭包（ldd 自身递归）。三个血泪坑：
#   1) 某些环境变量会毒化 ldd，统一在 env -i 下跑；
#   2) 必须用系统 /usr/bin/ldd——PATH 可能被别的发行环境（如 xlings subos）
#      里同名 ldd 遮蔽，对宿主 DT_RELR 二进制一律报错（openxlings/xlings#608）；
#   3) 管道尾的 while 会把 ldd 的非零退出码吞成 0，必须先落盘、校验、再消费。
EXCLUDES="libc.so libm.so libdl.so libpthread.so librt.so libresolv.so libnsl.so libutil.so libanl.so ld-linux libGL.so libGLX libOpenGL libGLdispatch libGLU libX11 libXext libXau libxcb libSM.so libICE.so libfontconfig libfreetype libharfbuzz libgraphite2 libudev libselinux libglib-2"
copy_deps_of() {  # copy_deps_of <标签> <二进制/库...>
    tag=$1; shift
    tmp=$(mktemp)
    if ! env -i /usr/bin/ldd "$@" > "$tmp"; then
        echo "ldd($tag) 失败——检查 /usr/bin/ldd 是否被 PATH 遮蔽" >&2
        rm -f "$tmp"; exit 1
    fi
    awk -v ex="$EXCLUDES" '
      BEGIN { split(ex, e, " ") }
      { n = $1; p = $3; if (p !~ /^\//) next;
        skip = 0; for (i in e) if (index(n, e[i]) == 1) { skip = 1; break }
        if (!skip) print p }' "$tmp" \
    | while read -r lib; do
        # 平铺成 SONAME 名（解引用符号链接，拷真实字节）
        cp -aL "$lib" "$DST/lib/$(basename "$lib")"
      done
    rm -f "$tmp"
}
copy_deps_of main "$SRC_BIN"

# flexiblas：provider 和真后端都是运行时 dlopen，不在任何 ldd 闭包里。
# provider 收进 lib/flexiblas/（引擎侧用 FLEXIBLAS_LIBRARY_PATH 指过去），
# rc 配置收进 etc/（FLEXIBLASRC_DIR 指过去），provider 的 DT_NEEDED
# （libopenblaso.so.0 等）再补一轮 ldd。
if [ -d "$LIBDIR/flexiblas" ]; then
    mkdir -p "$DST/lib/flexiblas" "$DST/etc"
    cp -a "$LIBDIR/flexiblas/"*.so "$DST/lib/flexiblas/"
    copy_deps_of flexiblas "$DST"/lib/flexiblas/*.so
    for f in /etc/flexiblasrc /etc/flexiblas64rc /etc/flexiblasrc.d /etc/flexiblas64rc.d; do
        { [ -e "$f" ] && cp -a "$f" "$DST/etc/"; } || true
    done
fi

cp -a "$PREFIX/share/octave" "$DST/share/octave"
cp -a "$LIBDIR/octave" "$DST/$(basename "$LIBDIR")/octave"

cat > "$DST/launcher.sh" <<'EOF'
#!/bin/sh
# 手动调试内置引擎：./engines/octave/launcher.sh --eval 'disp(1+1)'
# （ectave 应用本身不经此脚本，直接在 fork 子进程里 setenv + execv。）
D=$(cd "$(dirname "$0")" && pwd)
OCTAVE_HOME="$D"
LD_LIBRARY_PATH="$D/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
FLEXIBLAS_LIBRARY_PATH="$D/lib/flexiblas"
FLEXIBLAS_CONFIG="$D/etc/flexiblasrc"
export OCTAVE_HOME LD_LIBRARY_PATH FLEXIBLAS_LIBRARY_PATH FLEXIBLAS_CONFIG
exec "$D/bin/octave-cli" "$@"
EOF
chmod 755 "$DST/launcher.sh"

echo "== 完成："
du -sh "$DST/bin" "$DST/lib" "$DST/share" "$DST/$(basename "$LIBDIR")" "$DST" | sed 's/^/   /'

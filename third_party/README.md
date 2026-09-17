# third_party/ — 依赖源码

这里放**源码形式**的第三方依赖。目录内容不入库（`.gitignore` 已忽略 `eui-neo/`），
每台机器/每个 CI job 各自 clone。

## eui-neo/ — EUI-NEO（UI 框架）

ectave 的 UI 层就是 EUI-NEO。**不走包管理器**（旧版经 mcpp 包索引拿预编译的
`compat.eui-neo@0.5.9`），直接消费上游源码，为的是：能用上 EUI 的新提交，也能在本机
改 EUI 源码立刻重编验证。

```sh
git clone https://github.com/sudoevolve/EUI-NEO.git third_party/eui-neo
git -C third_party/eui-neo checkout dev      # 追上游开发线
```

`cmake/ectave_vendor.cmake` 按固定优先级挑一个 checkout：
`EUI_NEO_HOME`（环境变量，或 `-DEUI_NEO_HOME=`）→ `third_party/eui-neo` →
`../EUI-NEO`（同目录兄弟检出，仅本机便利）→ `FetchContent GIT_TAG dev`（兜底，需要网络）。

### Windows / macOS 由 CI 构建，本地只在 Linux 开发

`.github/workflows/build.yml` 里每个 job 都自己 clone 到 `third_party/eui-neo` 并
`checkout $EUI_NEO_COMMIT`（钉在 workflow 的 `env` 里），保证发行产物可复现。
**升级 EUI 的步骤**：本地 `git -C third_party/eui-neo pull` 拉到想用的提交，
跑通构建与冒烟，再把该提交的 SHA 写进 workflow 的 `EUI_NEO_COMMIT`。

### EUI 自己的第三方依赖不用管

EUI 把 glfw 3.4、glad、freetype 2.13.3、libpng 1.6.43、zlib 1.3.1、yyjson 0.12.0、
tray、md4c 0.5.3 都 vendored 在它自己的 `3rd/` 下（见上游 `3rd/README.md`），
`EUI_DEPS_MODE=bundled` 可完全离线。所以 ectave 这边**不需要**系统装 glfw/glew。

系统侧仍需的开发包（EUI 的 GLFW+OpenGL 后端与网络层要）：

| 平台 | 需要 |
| --- | --- |
| Fedora | `libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libcurl-devel` |
| Debian/Ubuntu | `libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libcurl4-openssl-dev` |
| macOS | `brew install llvm`（工具链本身，见 CI） |

另外注意：**EUI 的 `eui_neo_configure_app()` 不要用在 ectave 上**。它会顺带把
`-Os -fno-exceptions -fno-rtti` 加到目标上，和 ectave 满屏的 `import std;` 冲突
（std 模块是按开异常/RTTI 构建的）。根 `CMakeLists.txt` 里改成手动接线：
入口源 `core/app/glfw_app_main.cpp` + `eui::neo` + 自己写资源拷贝。

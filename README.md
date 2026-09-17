# ectave — EUI-NEO 套壳 GNU Octave 的桌面工作台

用 [EUI-NEO](https://github.com/sudoevolve/EUI-NEO)（C++ DSL 桌面 UI 框架）包裹
`octave` CLI 子进程，提供一个比终端 REPL 更顺手、又比官方 Octave-GUI 更轻的
数值计算前端。

构建：**CMake + C++23 模块**（`import std;`），UI 直接消费 EUI-NEO **上游源码**
（`third_party/eui-neo`，默认跟 `dev` 分支），不再经 mcpp 包索引拿预编译包。
工程形态对齐同目录姊妹项目 llm-switch / Clash-Flux / apitab：
`cmake/CxxImportStdGate.cmake` 门控 + `src/` 递归源码发现 + `.cppm` 显式走
`FILE_SET CXX_MODULES` + `tests/` 挂 CTest + CPack 出 Linux 安装包。

## 功能

- **控制台**：等宽字体（JetBrains Mono）、按行语义着色（输入回显 / 输出 /
  `error:` 红色 / `warning:` 琥珀 / ectave 自身消息蓝色），真实字体度量软换行，
  万行级输出用 virtualList 虚拟滚动，贴底自动跟随、上滚自动脱离。
- **命令栏**：Enter 执行 · Shift+Enter 换行（多行块）· ↑↓ 翻历史 · Esc 清空草稿。
  （内置 `components::input` 的 Escape 也会触发 onEnter，故命令栏用应用内派生
  组件 `src/ui/command_input.cppm`，复用 InputModel 全套选区/撤销/IME 逻辑。）
- **工作区面板**：命令执行完自动快照 `who + class + size`，单击变量直接执行
  `disp('name')` 查看内容。
- **脚本页**：多行编辑区整段一次发给 Octave，F5 / 「运行脚本」执行。
- **会话控制**：状态灯 + 版本、中断（Ctrl+C）、重启、清屏、工作区开关；
  优先使用内置引擎（`engines/octave/`），没有内置时回落 PATH 上的 octave，
  都没有时给友好提示并支持一键重启。
- **三平台**：Linux/macOS 用 `forkpty` 真 PTY，Windows 用 ConPTY
  （`CreatePseudoConsole`，需 Win10 1809+）——同一个「让 octave 认为是真人
  终端」的东西，协议与线程模型两平台完全共用。

快捷键：`Ctrl+L` 清屏 · `Ctrl+1/2` 切页 · `F5` 运行脚本。

## 依赖

- **UI 框架**：EUI-NEO 上游源码，放 `third_party/eui-neo`（已 gitignore）：

  ```sh
  git clone https://github.com/sudoevolve/EUI-NEO.git third_party/eui-neo
  git -C third_party/eui-neo checkout dev
  ```

  `cmake/ectave_vendor.cmake` 的选择顺序：`EUI_NEO_HOME` 环境变量 →
  `third_party/eui-neo` → 同目录 `../EUI-NEO`（本机便利）→ `FetchContent`。
  细节与「怎么升级 EUI」见 `third_party/README.md`。

  **EUI 自带 vendored 依赖**（glfw 3.4 / glad / freetype 2.13.3 / libpng / zlib /
  yyjson / tray / md4c，都在它自己的 `3rd/` 下），所以本机**不需要**装 glfw/glew。
  系统侧只需要 X11/GL/curl 的开发包（Fedora：
  `libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libcurl-devel`；
  本机 GCC 16 即可，CI 的 Linux 用 clang-21 + libc++，见 workflow 顶部注释）。

- **运行期 Octave** 有两种来源（引擎查找顺序：**内置 → PATH**）：
  - **内置**：`sh scripts/build_engines.sh` 把本机 Octave 打包成自包含的
    `engines/octave/`（OCTAVE_HOME 树：`bin/` + `lib/`（ldd 闭包，剔除 glibc
    与图形/窗口栈——能跑 ectave 的机器必有、打包反而和 Mesa 驱动打架）+
    `share/octave/` + `lib64/octave/` + flexiblas provider/配置 + `launcher.sh`），
    约 250MB。`engines/` 已 gitignore，随发布包携带、不入库。
    子进程由引擎直接 `setenv(OCTAVE_HOME, LD_LIBRARY_PATH, FLEXIBLAS_LIBRARY_PATH,
    FLEXIBLAS_CONFIG)` 后绝对路径 `execv`，无需经 launcher.sh（那只是手动调试入口）。
    在打包机上运行脚本即可（按 `__octave_config_info__` 自适应 lib/lib64 布局；
    换 Debian multiarch 打包需核对 home 树布局）。
  - **系统**：装 `sudo dnf install octave`（或 apt / brew / Windows 官网安装包）
    后直接可用，PATH 上的 `octave-cli` → `octave`（Windows 侧按 PATHEXT 试
    `.exe` 等后缀）。内置引擎树是 Linux 打包脚本的产物，Windows 走这一档。
  - `ECTAVE_OCTAVE_HOME=<dir>` 可显式指定内置引擎根（目录须含
    `bin/octave-cli`）。查找同时参考 `/proc/self/exe`（Linux）/`_NSGetExecutablePath`
    （macOS）与 `/proc/self/cmdline[0]`、cwd 逐级上溯。

## 构建与运行

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure        # 领域层单元测试
./run.sh                                          # 启动 GUI（cd 到 build/ 再起）
sh scripts/build_engines.sh                       # 可选：打包内置引擎 engines/octave/
```

或直接用预设：`cmake --preset ninja-release && cmake --build --preset ninja-release`
（调试用 `ninja-debug`，产物在 `build-debug/`）。

`run.sh` 只做两件事：`cd` 到构建目录（EUI 按相对路径找字体）、注入
`INTEL_FORCE_PROBE=1`（本机 Arc 显卡需要）。旧 mcpp 版本里那层
`/lib64/ld-linux-x86-64.so.2 --inhibit-rpath '' --library-path ...` 包装已经删掉
——它存在的前提（mcpp 私有 glibc 与发行版 Mesa/GLX 版本冲突）随构建系统一起消失。

## 发布（GitHub Actions）

`.github/workflows/build.yml`：推 `v*` tag 时在 Windows / Linux / macOS 三平台
各构建一次，打包并创建 **draft** Release（人工点发布）；`workflow_dispatch`
可手动跑一遍只出 artifact。

```sh
git tag v0.2.0 && git push origin v0.2.0   # 触发
```

- 产物：Linux → `tar.gz` + `deb` + `rpm`，Windows → `zip`，macOS → `tar.gz`。
  包结构一致：`ectave[.exe]` + `assets/` + `README.md`（用户向说明，源在
  `packaging/dist-README.md`）；Linux 额外带 `run.sh`。
- LINUX 的 `assets/` 由 CMake POST_BUILD 铺好（EUI 默认资源 + 本项目字体），
  CI 直接把构建目录里的 `ectave` + `assets/` 一起作为「运行时目录」交给
  `packaging/ci-package.sh`，不再拼装。
- **不打内置 Octave 引擎**：`engines/octave` 是在有 octave 的 Linux 机器上用
  `scripts/build_engines.sh` 生成的（约 250MB），CI 上既没有 octave 也不该把
  它塞进每个 artifact，因此发行包走「PATH 上的 octave」这一档；本地手动打包时
  若 `engines/octave` 已存在，`packaging/ci-package.sh` 会自动一并带上。
- CI 各 job 自己 clone EUI 到 `third_party/eui-neo` 并 `checkout $EUI_NEO_COMMIT`
  （钉在 workflow 的 `env` 里），保证发行产物可复现。
- Linux 包带 clang 的 C++ 运行时（`lib/` + `$ORIGIN/lib` RUNPATH）。

## 架构

| 模块 | 职责 |
| --- | --- |
| `CMakeLists.txt` | 顶层构建：EUI 选择 → 目标 + 模块 FILE_SET → 资源铺设 → 平台链接选项 → 安装/CPack |
| `cmake/CxxImportStdGate.cmake` | `import std` 的 experimental UUID 门控（逐 CMake 版本） |
| `cmake/ectave_vendor.cmake` | EUI-NEO 源码四级优先选择 |
| `src/app.cpp` | 配置、常驻壳（工具栏/状态栏/页面分发）、全局按键、启动引擎 |
| `src/octave_engine.cppm` | octave 子进程（POSIX `forkpty` / Windows `ConPTY` 双实现，协议与读线程共用）、内置引擎查找与环境装配、哨兵分块协议、事件信箱 |
| `src/store.cppm` | 会话状态：控制台行、历史、工作区、视图位置（仅 UI 线程读写） |
| `src/ui/*.cppm` | 页面渲染与设计令牌；后台回调只 enqueue + `requestUiUpdate()`，UI 在 `compose()` 排空 |
| `tests/` | CTest 单元测试：`ectave.utils` 字符串/UTF-8、`ectave.store` 行模型/历史/输出分类 |
| `scripts/build_engines.sh` | 内置引擎打包：ldd 闭包平铺（剔除 glibc/图形栈）+ share/lib64 资源树 + flexiblas provider 与 rc |
| `packaging/ci-package.{sh,ps1}` | 发布打包（不编译）：运行时目录 + 用户向 README → tar.gz / zip |
| `.github/workflows/build.yml` | 三平台构建 → 测试 → 打包 → tag 上建 draft Release |

`main()` 由 EUI 的 `core/app/glfw_app_main.cpp` 提供，CMake 里作为 ectave 目标的
一个源文件加进来——这是旧 mcpp 配置里 `app-main` feature 的等价物。
**不要**改用 EUI 的 `eui_neo_configure_app()`：它会顺带把 `-Os -fno-exceptions
-fno-rtti` 加到目标上，与满屏的 `import std;` 冲突（std 模块按开异常/RTTI 构建）。

协议：octave 跑在 `forkpty` 造出的真 PTY 里——管道模式会进入批处理语义，
任何 `error()` 或语法错误直接终止解释器（Octave 10 移除了
`set_error_handler`，try/catch 罩不住 parse error），而 REPL 的日常就是打错
命令；PTY 里报错回到提示符，中断 = 写一个 `0x03`（Ctrl+C）。每条命令追加
`disp('%<sentinel>%')`，读线程按行精确匹配哨兵判定块结束——stdin 有序 ⇒
哨兵按序返回，天然支持命令排队。回显行按引导里设死的 `PS1 = "ec> "` 前缀
整行丢弃（控制台回显由 UI 自己渲染）。工作区用 `ECTVAR|name|class|dims`
前缀行回传，不进控制台。

## 已知边界

- 图形：`--no-window-system` 下 `plot()` 不开窗口（需要图时用
  `print('/tmp/f.png','-dpng')` 再自行查看）；GUI 内嵌绘图是后续方向。
- 中断依赖 octave 对 SIGINT 的处理，极端情况下可能需要「重启」清会话。
- Windows/macOS 分支已实现（ConPTY / `forkpty`；Windows 的 GUI 子系统链接选项与
  macOS 的 `-lutil` 在根 `CMakeLists.txt` 里按平台加），但两侧只在 CI 里验证到
  「编译 + 测试 + 打包」——运行期尚未在真机跑过（Linux 侧含内置引擎已端到端
  验证）。Windows 需要 Win10 1809+（低于此版本会报 conpty-unavailable 而不崩）。
- 内置引擎依赖目标机的 glibc 与图形/窗口栈（ectave 本体同样需要，故不算额外
  负担）；打包脚本须在有 octave 的机器上运行，脚本硬用 `/usr/bin/ldd` 以免被
  PATH 上其他发行环境的同名 ldd 遮蔽（xlings subos 的坑，openxlings/xlings#608）。
- `plot()` 需要 gnuplot 时仍走系统安装（未打包外部图形工具链）。
- 本机开发用系统 GCC 16；CI 的 Linux 用 clang-21 + libc++（ubuntu 的 GCC 13/14
  不支持 `import std`，26.04 的 g++-16 快照会 ICE），Windows 用 MSVC，macOS 用
  brew LLVM。这条链差异是已知的，改动构建选项时两边都要过一遍。

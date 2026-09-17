# ectave — EUI-NEO 套壳 GNU Octave 的桌面工作台

用 [EUI-NEO](../../EUI-NEO)（C++ DSL 桌面 UI 框架）包裹 `octave` CLI 子进程，
提供一个比终端 REPL 更顺手、又比官方 Octave-GUI 更轻的数值计算前端。
mcpp + C++23 模块构建（与 tinynext 同一工程模式：`app-main` feature 提供
`main()`，`src/app.cpp` 只提供 `app::dslAppConfig() + app::compose()`）。

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
- **会话控制**：状态灯 + 版本、中断（SIGINT）、重启、清屏、工作区开关；
  优先使用内置引擎（`engines/octave/`），没有内置时回落 PATH 上的 octave，
  都没有时给友好提示并支持一键重启。

快捷键：`Ctrl+L` 清屏 · `Ctrl+1/2` 切页 · `F5` 运行脚本。

## 依赖与内置 Octave

- 构建用 mcpp 私有工具链（llvm@22.1.8），依赖 `compat.eui-neo@0.5.9`（自动解析）。
- 运行期 Octave 有两种来源（引擎查找顺序：**内置 → PATH**）：
  - **内置**：`sh scripts/build_engines.sh` 把本机 Octave 打包成自包含的
    `engines/octave/`（OCTAVE_HOME 树：`bin/` + `lib/`（ldd 闭包，剔除 glibc
    与图形/窗口栈——能跑 ectave 的机器必有、打包反而和 Mesa 驱动打架）+
    `share/octave/` + `lib64/octave/` + flexiblas provider/配置 + `launcher.sh`），
    约 250MB。`engines/` 已 gitignore，随发布包携带、不入库。
    子进程由引擎直接 `setenv(OCTAVE_HOME, LD_LIBRARY_PATH, FLEXIBLAS_LIBRARY_PATH,
    FLEXIBLAS_CONFIG)` 后绝对路径 `execv`，无需经 launcher.sh（那只是手动调试入口）。
    在打包机上运行脚本即可（按 `__octave_config_info__` 自适应 lib/lib64 布局；
    换 Debian multiarch 打包需核对 home 树布局）。
  - **系统**：装 `sudo dnf install octave`（或 apt）后直接可用，PATH 上的
    `octave-cli` → `octave`。
  - `ECTAVE_OCTAVE_HOME=<dir>` 可显式指定内置引擎根（目录须含
    `bin/octave-cli`）。exe 经 `run.sh` 的系统 ld.so 加载时 `/proc/self/exe`
    指向 loader，因此查找同时参考 `/proc/self/cmdline[0]` 与 cwd 逐级上溯。

## 构建与运行

```sh
mcpp build            # debug
mcpp build --release  # release
sh scripts/build_engines.sh   # 可选：把本机 octave 打包成内置引擎 engines/octave/
./run.sh              # 启动 GUI（从项目根目录启动，assets/ 字体按相对路径解析）
```

`run.sh` 经系统 `ld.so` 加载（原因同 tinynext：mcpp 私有 glibc 与本机 Mesa/GLX
的 GLIBC 版本不匹配），并注入 `INTEL_FORCE_PROBE=1`（本机 Arc 显卡需要）。

## 架构

| 模块 | 职责 |
| --- | --- |
| `src/app.cpp` | 配置、常驻壳（工具栏/状态栏/页面分发）、全局按键、启动引擎 |
| `src/octave_engine.cppm` | octave 子进程（forkpty 交互式 PTY）、内置引擎查找与环境装配、哨兵分块协议、读线程事件信箱 |
| `src/store.cppm` | 会话状态：控制台行、历史、工作区、视图位置（仅 UI 线程读写） |
| `src/ui/*.cppm` | 页面渲染与设计令牌；后台回调只 enqueue + `requestUiUpdate()`，UI 在 `compose()` 排空 |
| `scripts/build_engines.sh` | 内置引擎打包：ldd 闭包平铺（剔除 glibc/图形栈）+ share/lib64 资源树 + flexiblas provider 与 rc |

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
- 未做 Windows/macOS 分支（引擎是 POSIX forkpty 实现）。
- 内置引擎依赖目标机的 glibc 与图形/窗口栈（ectave 本体同样需要，故不算额外
  负担）；打包脚本须在有 octave 的机器上运行，脚本硬用 `/usr/bin/ldd` 以免被
  PATH 上其他发行环境的同名 ldd 遮蔽（xlings subos 的坑，openxlings/xlings#608）。
- `plot()` 需要 gnuplot 时仍走系统安装（未打包外部图形工具链）。
- 无 `mcpp test` 目标：`app-main` 已提供 `main()`，与测试二进制冲突（同 tinynext）。

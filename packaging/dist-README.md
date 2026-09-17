# ectave — EUI-NEO 套壳 GNU Octave 的桌面工作台

（版本号见压缩包文件名。）

把 `octave` CLI 跑在真终端（Linux/macOS 用 PTY，Windows 用 ConPTY）里，套一层
桌面 UI：按行着色控制台、历史、工作区变量面板、脚本页。

## 启动

| 平台 | 怎么启动 |
| --- | --- |
| Linux x86_64（tar.gz） | `./run.sh` |
| Linux x86_64（.deb/.rpm） | 装完直接运行 `ectave`（应用菜单里也能找到） |
| macOS arm64 | `./ectave` |
| Windows x64 | 双击 `ectave.exe`（或命令行运行） |

`assets/` 里是随包的字体（JetBrains Mono / Noto Sans SC / Font Awesome）。
在解压出的目录里启动最省事；就算在别处启动也没关系——程序按
「当前目录 → 可执行文件旁边的 assets/」的顺序找字体，压缩包解压后这两者是同一个
目录。

## 还需要一个 Octave

本包**不含 Octave 解释器**，启动时会按顺序找：

1. `engines/octave/bin/octave-cli`（内置引擎，本包默认没有——见下）
2. `PATH` 上的 `octave-cli`
3. `PATH` 上的 `octave`

所以先装一个 Octave 即可：

- **Windows**：官方安装包 <https://octave.org/download>（安装时勾选「Add to PATH」）
- **macOS**：`brew install octave`
- **Linux**：`sudo dnf install octave` / `sudo apt install octave`

都没有时 ectave 会在控制台给一行提示，装好后点工具栏「重启」即可。

### 想要不依赖系统 Octave（自包含）

在**已装 Octave 的 Linux 机器**上，从源码仓库运行：

```sh
sh scripts/build_engines.sh      # 生成 engines/octave/（约 250MB）
```

然后把生成的 `engines/octave/` 放到本程序同级目录，程序会自动优先使用它
（也可以用 `ECTAVE_OCTAVE_HOME=<dir>` 显式指定）。脚本把本机 Octave 连同
`ldd` 依赖闭包、`share/octave` 资源树、flexiblas provider 一起打包成
`OCTAVE_HOME` 树；剔除 glibc 与图形/窗口栈（运行 ectave 的机器本来就有，
打包反而会和发行版的 Mesa 驱动打架）。

## 已知边界

- `plot()` 不开窗口（引擎以 `--no-window-system` 启动）；需要图时用
  `print('/tmp/f.png','-dpng')` 再自行查看。
- 中断靠往终端写 `0x03`（Ctrl+C）实现，极端情况下可能需要「重启」清会话。
- 内置引擎树是 Linux 的打包脚本产物；Windows/macOS 请用系统 Octave。
- Linux 压缩包里的 `lib/` 是 clang 的 C++ 运行时（libc++ / libc++abi /
  libunwind，多数发行版不预装），可执行文件通过 `$ORIGIN/lib` 的 RUNPATH 加载，
  整个目录一起搬走即可，别只拷可执行文件。系统侧只需要常规的 glibc 与
  图形栈（Mesa/GLX）。

## 许可

Apache-2.0。随包字体：JetBrains Mono（OFL）、Noto Sans SC（OFL，
见 `assets/NotoSansSC-OFL.txt`）、Font Awesome Free（CC BY 4.0）。

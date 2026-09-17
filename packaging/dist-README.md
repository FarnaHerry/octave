# ectave v0.1.0 — EUI-NEO 套壳 GNU Octave 的桌面工作台

把 `octave` CLI 跑在真终端（Linux/macOS 用 PTY，Windows 用 ConPTY）里，套一层
桌面 UI：按行着色控制台、历史、工作区变量面板、脚本页。

## 启动

| 平台 | 怎么启动 |
| --- | --- |
| Linux x86_64 | `./run.sh`（内含系统 ld.so 加载脚本，见下） |
| macOS arm64 | `./ectave` |
| Windows x64 | 双击 `ectave.exe`（或命令行运行） |

`assets/` 里是随包的字体（JetBrains Mono / Noto Sans SC / Font Awesome），
请**在解压出的目录里启动**，程序按相对路径读字体。

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

## 已知边界（v0.1.0）

- `plot()` 不开窗口（引擎以 `--no-window-system` 启动）；需要图时用
  `print('/tmp/f.png','-dpng')` 再自行查看。
- 中断靠往终端写 `0x03`（Ctrl+C）实现，极端情况下可能需要「重启」清会话。
- 内置引擎树是 Linux 的打包脚本产物；Windows/macOS 请用系统 Octave。
- Linux 包里的 `run.sh` 绕了一层系统 `ld.so`：mcpp 自带的 glibc 与发行版
  图形栈（Mesa/GLX）的 GLIBC 版本可能对不上，用系统加载器起更稳。
  需要 glibc ≥ 2.39 的桌面环境。

## 许可

Apache-2.0。随包字体：JetBrains Mono（OFL）、Noto Sans SC（OFL，
见 `assets/NotoSansSC-OFL.txt`）、Font Awesome Free（CC BY 4.0）。

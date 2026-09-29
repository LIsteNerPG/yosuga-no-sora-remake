# Linux 构建与如意玲珑打包

本文说明如何编译、运行并把《缘之空：高清重制》打包成 Linux 桌面应用，以及如何
生成如意玲珑（Linglong / linyaps）格式的安装包。

Linux 版本与 Windows/macOS 版本共用同一套 Kirikiri SDL2 引擎源码（`src/`），
区别只在于平台后端、安装布局和打包方式。

## 运行环境要求

| 项目 | 要求 |
| --- | --- |
| 架构 | x86_64（玲珑 base 同时支持 arm64/loong64，源码未做架构限制） |
| 显示 | X11 或 Wayland（SDL2 静态链接，两种后端都会自动探测） |
| 图形 | OpenGL 2.1+ / OpenGL ES 2.0+（Mesa 软件渲染 `llvmpipe` 可用） |
| 音频 | ALSA 或 PulseAudio（FAudio 经 SDL2 音频子系统输出） |

## 开发构建

开发构建直接读取仓库的 `data/` 目录，不做安装、不复制资源：

```sh
./setup.sh                 # 首次：拉取子模块并下载游戏数据
./project.sh run linux-sdl2
```

`project.sh` 会把 `data/` 目录作为参数传给引擎（引擎把非选项参数当作项目目录），
因此修改脚本或资源后不需要重新打包。构建目录为 `build/dev/linux-sdl2/`。

引擎选项可以直接追加，例如：

```sh
./project.sh run linux-sdl2 -about
```

编译所需的系统依赖（Debian/Ubuntu 名称）：

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxinerama-dev libxss-dev libxkbcommon-dev libwayland-dev \
  wayland-protocols libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev \
  libgles2-mesa-dev libasound2-dev libpulse-dev libdbus-1-dev \
  libudev-dev libunwind-dev libfontconfig1-dev
```

## 安装布局

`cmake --install` 使用 freedesktop 约定，安装到 `CMAKE_INSTALL_PREFIX`
（默认 `/usr/local`，玲珑构建中为 `/opt/apps/<id>/files`）：

```
<prefix>/bin/krkrsdl2
<prefix>/share/yosuga-no-sora-remake/data/…          # 完整游戏内容
<prefix>/share/applications/io.github.shuimo0413.yosuganosora.desktop
<prefix>/share/icons/hicolor/<size>x<size>/apps/yosuganosora.png
```

相关 CMake 选项：

| 选项 | 默认 | 说明 |
| --- | --- | --- |
| `KRKRSDL2_INSTALL_LINUX_DATA` | `ON` | 安装 `data/` 游戏内容；关闭后只安装引擎与桌面文件 |
| `KRKRSDL2_LINUX_APP_ID` | `io.github.shuimo0413.yosuganosora` | 反向域名应用 ID，同时用作 `.desktop` 文件名与存档目录名 |
| `KRKRSDL2_ICON_NAME` | `yosuganosora` | hicolor 图标基名，需与 `.desktop` 的 `Icon=` 一致 |
| `KRKRSDL2_LINUX_DATA_SUBDIR` | `share/yosuga-no-sora-remake` | 相对于前缀的数据目录 |

`.desktop` 的 `Exec=` 与 `linglong.yaml` 的 `command` 都指向
`<prefix>/bin/krkrsdl2 <prefix>/share/yosuga-no-sora-remake/data`，二者由同一套
CMake 变量派生，因此不会出现“双击能启动、命令行启不动”的偏差。

## 存档位置

Linux 版遵循 XDG 规范，存档写入：

```
${XDG_DATA_HOME:-$HOME/.local/share}/io.github.shuimo0413.yosuganosora/
```

玲珑容器会把宿主机的 `$HOME` 映射进来，因此存档在应用更新后仍然保留，也可以
直接备份或替换。设置 `KRKR_LINUX_SAVE_DIR` 可以覆盖该位置（便携/测试用）。

## 如意玲珑打包

玲珑（linyaps）是 deepin 主导的 Linux 应用打包格式：应用连同依赖运行在基于
`base` 的容器中，产物是单一 UAB（`ll-cli install xxx.uab`）或已弃用的 layer 包。

本仓库根目录就是玲珑构建工程根：`ll-builder` 会把仓库挂载为容器内的 `/project`，
`linglong.yaml` 的 `build` 脚本在其中直接调用 CMake。工程**不使用 `sources`**——
源码、子模块（`external/`）与游戏数据（`data/`）全部来自构建工作区，因此构建前
数据必须已经下载到 `data/`。

### 准备构建机

```sh
# Debian 12/13、Ubuntu 24.04 等
echo 'deb [trusted=yes] https://ci.deepin.com/repo/obs/linglong:/CI:/release/Ubuntu_24.04/ ./' \
  | sudo tee /etc/apt/sources.list.d/linglong.list
sudo apt update
sudo apt install linglong-bin linglong-builder

# Ubuntu 24.04 默认限制非特权用户命名空间，玲珑容器需要放开
sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0
```

> 发行版目录名是 `Ubuntu_24.04`；部分旧文档写作 `xUbuntu_24.04`，该路径返回 404。

### 构建与导出

```sh
python tools/fetch_data_parts.py --dest data     # 必需：内嵌完整游戏内容
ll-builder build                                 # 编译 + 安装到 $PREFIX + 提交到本地缓存
ll-builder export -z zstd \
  --icon src/resources/linux/icons/hicolor/256x256/apps/yosuganosora.png \
  -o Yosuga-no-Sora-HD-Remake-Linux-x86_64.uab
```

旧的 layer 格式（供 1.14 之前的玲珑版本使用）：

```sh
ll-builder export --layer --no-develop
```

### 安装与运行

```sh
sudo ll-cli install ./Yosuga-no-Sora-HD-Remake-Linux-x86_64.uab
ll-cli run io.github.shuimo0413.yosuganosora
```

也可以直接从应用菜单启动（安装时会注册 `.desktop` 文件与图标）。

### 关于包体积

游戏内容约 4 GB，且按“随包分发”的方式内嵌（与 Windows/macOS 版本一致），因此
UAB 文件通常在 2.5–3.5 GB 之间，超过 GitHub Release 单文件 2 GiB 的限制。发布
工作流会按现有约定用 7-Zip 切成 `*.7z.001`、`*.7z.002`… 分卷，下载后打开
`.7z.001` 解压即可得到完整 UAB。

## 持续集成

`.github/workflows/release-linux-linglong.yml` 在 `v*-linux-*` 标签推送或手动
触发时运行，支持两种模式：

- **compile-check**：只安装编译依赖并构建 `krkrsdl2`，用于快速验证 Linux 目标
  仍可编译（不需要下载 4 GB 游戏数据）。
- **full**（默认）：安装玲珑构建器、下载并校验游戏数据、注入版本号、`ll-builder
  build`、可选 Xvfb 无头冒烟测试、导出 UAB/layer、7-Zip 分卷并发布到 GitHub
  Release。

手动触发时可以选择导出格式（`both`/`uab`/`layer`）以及是否运行冒烟测试。

## 故障排查

| 现象 | 原因与处理 |
| --- | --- |
| `ll-builder build` 报 box 启动失败 | 内核限制了非特权用户命名空间，执行 `sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0` |
| 启动后窗口空白 | 缺少 OpenGL 驱动；确认宿主机已安装 Mesa 驱动，玲珑环境按官方说明安装对应显卡驱动包 |
| 没有声音 | 玲珑容器未挂载音频服务；确认宿主机 PulseAudio/PipeWire 正在运行 |
| 提示找不到 `data` | 玲珑包内数据缺失，通常是构建前忘记 `python tools/fetch_data_parts.py --dest data` |
| 中文字体显示为方块 | 游戏自带 `data/font/`，若被裁剪请确认该目录已随包安装 |

## 相关文件

| 路径 | 说明 |
| --- | --- |
| `linglong.yaml` | 玲珑构建配置（包元信息、base、构建依赖、构建脚本） |
| `src/resources/linux/yosuganosora.desktop.in` | `.desktop` 模板 |
| `src/resources/linux/icons/hicolor/` | 多尺寸图标（由 `icon.ico` 生成） |
| `tools/update_linglong_version.py` | 按发布标签改写 `linglong.yaml` 版本号 |
| `project.sh` | 开发启动器（`run linux-sdl2`） |

#!/usr/bin/env bash
# 收集 Linux（含如意玲珑容器）上运行《缘之空：高清重制》所需的诊断信息。
#
# 用法：
#   tools/diagnose_linux.sh [应用ID]
#
# 脚本只读取系统与容器状态，最后打印一组建议的启动命令，不会修改任何东西。
# 把完整输出贴回 issue 即可定位绝大多数画面/启动问题。

set -uo pipefail

APP_ID="${1:-io.github.shuimo0413.yosuganosora}"
FILES="/opt/apps/${APP_ID}/files"
DATA="${FILES}/share/yosuga-no-sora-remake/data"

section() { printf '\n===== %s =====\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

section "系统"
grep -E '^(PRETTY_NAME|VERSION_ID)=' /etc/os-release 2>/dev/null
uname -srm
printf 'desktop=%s\n' "${XDG_CURRENT_DESKTOP:-${DESKTOP_SESSION:-?}}"

section "显示会话"
printf 'XDG_SESSION_TYPE=%s\n' "${XDG_SESSION_TYPE:-?}"
printf 'WAYLAND_DISPLAY=%s\n' "${WAYLAND_DISPLAY:-<unset>}"
printf 'DISPLAY=%s\n' "${DISPLAY:-<unset>}"
ls -l /tmp/.X11-unix 2>/dev/null | head -n 5
printf 'XDG_RUNTIME_DIR=%s\n' "${XDG_RUNTIME_DIR:-<unset>}"
ls "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" 2>/dev/null | grep -Ei 'wayland|pulse|pipewire' || true

section "屏幕与缩放"
if have xrandr; then xrandr --current 2>/dev/null | grep -E ' connected|\*'; fi
if have wlr-randr; then wlr-randr 2>/dev/null | head -n 20; fi
printf 'GDK_SCALE=%s QT_SCALE_FACTOR=%s\n' "${GDK_SCALE:-<unset>}" "${QT_SCALE_FACTOR:-<unset>}"

section "GPU / 驱动"
ls -l /dev/dri 2>/dev/null || echo '/dev/dri 不存在'
for m in nvidia amdgpu i915 nouveau virtio_gpu vmwgfx vboxvideo; do
  if [ -d "/sys/module/$m" ]; then printf "module %s: %s\n" "$m" "$(cat "/sys/module/$m/version" 2>/dev/null || echo present)"; fi
done
if have glxinfo; then glxinfo -B 2>/dev/null | grep -Ei 'OpenGL renderer|OpenGL version|Device|Accelerated'; else echo 'glxinfo 未安装（apt install mesa-utils）'; fi

section "音频"
printf 'PULSE_SERVER=%s\n' "${PULSE_SERVER:-<unset>}"
pactl info 2>/dev/null | grep -E 'Server Name|Server Version' || echo 'pactl 不可用'

section "玲珑"
if have ll-cli; then
  ll-cli --version 2>/dev/null
  ll-cli list 2>/dev/null | head -n 20
  echo '--- ll-cli info ---'
  ll-cli info "$APP_ID" 2>/dev/null | head -n 40
else
  echo 'll-cli 未安装'
fi

section "容器内自检"
if have ll-cli; then
  ll-cli run "$APP_ID" -- /bin/sh -c "
    echo '--- 安装内容 ---'
    ls -l '${FILES}/bin/' 2>/dev/null
    du -sh '${DATA}' 2>/dev/null
    ls '${DATA}/startup.tjs' 2>/dev/null || echo 'startup.tjs 缺失'
    echo '--- 动态库解析 ---'
    ldd '${FILES}/bin/krkrsdl2' 2>/dev/null | grep -E 'not found|=>' | head -n 40
    echo '--- 容器内 GL ---'
    ls /usr/lib/x86_64-linux-gnu/dri 2>/dev/null | head -n 10
  " 2>&1 | head -n 80
fi

section "建议的启动命令（按顺序尝试，观察哪一组画面正常）"
cat <<EOF
# 1) 默认启动，把引擎日志打到终端
ll-cli run ${APP_ID} ${FILES}/bin/krkrsdl2 ${DATA} -forceoutputlogtoconsole

# 2) 强制 X11（Deepin 25 若跑在 Wayland 会话下，XWayland 往往更稳）
ll-cli run --env SDL_VIDEODRIVER=x11 ${APP_ID} ${FILES}/bin/krkrsdl2 ${DATA} -forceoutputlogtoconsole

# 3) 强制软件渲染（虚拟机没有 3D 加速时的常见修法）
ll-cli run --env SDL_VIDEODRIVER=x11 --env LIBGL_ALWAYS_SOFTWARE=1 --env SDL_RENDER_DRIVER=software \
  ${APP_ID} ${FILES}/bin/krkrsdl2 ${DATA} -forceoutputlogtoconsole

# 4) OpenGL 走 Mesa 的软件光栅器
ll-cli run --env SDL_VIDEODRIVER=x11 --env GALLIUM_DRIVER=llvmpipe \
  ${APP_ID} ${FILES}/bin/krkrsdl2 ${DATA} -forceoutputlogtoconsole
EOF

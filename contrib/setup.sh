#!/bin/sh
# betterframe-xp: one-command setup for Steam Frame (SteamOS, aarch64). User space only, no sudo.
#
#   sh contrib/setup.sh              check -> build -> install -> enable the layer and the panel
#   sh contrib/setup.sh --check      only check this machine (changes nothing, builds nothing)
#   sh contrib/setup.sh --no-panel   layer only (no dashboard panel)
#   sh contrib/setup.sh --no-enable  install only; enable later with: betterframectl enable
#
# Safe to run again (it updates). It NEVER restarts SteamVR: the layer is picked up at the next
# SteamVR start, which you do yourself when you are not playing.
set -u

src_dir=$(cd "$(dirname "$0")/.." && pwd)
check_only=0; want_panel=1; want_enable=1
for a in "$@"; do
    case "$a" in
        --check) check_only=1 ;;
        --no-panel) want_panel=0 ;;
        --no-enable) want_enable=0 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $a (see --help)" >&2; exit 2 ;;
    esac
done

say()  { printf '%s\n' "$*"; }
ok()   { printf '  [ok]   %s\n' "$*"; }
warn() { printf '  [warn] %s\n' "$*"; warnings=$((warnings + 1)); }
bad()  { printf '  [NG]   %s\n' "$*"; problems=$((problems + 1)); }
warnings=0; problems=0

say '== betterframe-xp setup =='
say 'Checking this machine...'

# --- who / where -------------------------------------------------------------
if [ "$(id -u)" = 0 ]; then
    bad 'running as root. Run as the normal user (steamos): files go to ~/.local and ~/.config'
else
    ok "user: $(id -un) (no root needed)"
fi

arch=$(uname -m)
if [ "$arch" = aarch64 ]; then ok "CPU: $arch"
else bad "CPU is $arch. This is made for Steam Frame (aarch64); the layer is only useful there"; fi

if [ -r /etc/os-release ] && grep -q '^ID=steamos' /etc/os-release; then
    ok "OS: SteamOS $(sed -n 's/^VERSION_ID=//p' /etc/os-release)"
else
    warn 'this does not look like SteamOS. Nothing is known to work elsewhere'
fi

# --- build tools -------------------------------------------------------------
missing=''
for c in meson ninja cmake gcc g++ glslangValidator pkg-config install; do
    command -v "$c" >/dev/null 2>&1 || missing="$missing $c"
done
if [ -n "$missing" ]; then
    bad "missing build tools:$missing"
else
    ok 'build tools: meson ninja cmake gcc g++ glslangValidator pkg-config'
fi

if [ -f /usr/include/vulkan/vulkan.h ]; then ok 'Vulkan headers'
else bad 'Vulkan headers not found (/usr/include/vulkan/vulkan.h)'; fi

# --- panel (optional) --------------------------------------------------------
openvr_lib=/opt/steamvr/bin/linuxarm64
if [ "$want_panel" = 1 ]; then
    panel_ok=1
    for m in cairo freetype2 vulkan; do
        if ! pkg-config --exists "$m" 2>/dev/null; then
            bad "panel: development files for '$m' not found"; panel_ok=0
        fi
    done
    if [ ! -f "$openvr_lib/libopenvr_api.so" ]; then
        bad "panel: $openvr_lib/libopenvr_api.so not found (is SteamVR installed?)"; panel_ok=0
    fi
    if [ ! -d /usr/share/fonts/noto-cjk ]; then
        bad 'panel: /usr/share/fonts/noto-cjk not found. The panel text is Japanese and will not show'; panel_ok=0
    fi
    if [ "$panel_ok" = 1 ]; then ok 'panel: libraries, OpenVR, fonts'
    else say '         -> the panel is part of the install, so setup stops here. For the layer only: --no-panel'; fi
fi

# --- state / disk ------------------------------------------------------------
free_kb=$(df -Pk "$HOME" 2>/dev/null | awk 'NR==2{print $4}')
if [ -n "${free_kb:-}" ] && [ "$free_kb" -lt 524288 ]; then bad "less than 512 MB free in \$HOME"
else ok 'disk space in $HOME'; fi

if pgrep -x vrserver >/dev/null 2>&1; then
    ok 'SteamVR is running now (it will not be touched; restart it yourself later)'
fi
if [ -f "$HOME/.local/bin/betterframectl" ]; then
    say "  [info] already installed here: this run updates it ($("$HOME/.local/bin/betterframectl" status 2>/dev/null | head -1))"
fi

if [ "$problems" -gt 0 ]; then
    say ''
    say "Cannot continue: $problems problem(s) above (marked [NG]). Nothing was changed."
    say 'See "Requirements" in the README. SteamOS 0.5.6 had all of these preinstalled.'
    exit 1
fi
if [ "$check_only" = 1 ]; then
    say ''
    say "Check passed ($warnings warning(s)). Nothing was changed. Run without --check to install."
    exit 0
fi

# --- build + install ---------------------------------------------------------
say ''
say 'Building and installing (about 1 minute)...'
log=$(mktemp "${TMPDIR:-/tmp}/betterframe-xp-setup.XXXXXX")
if [ "$want_panel" = 1 ]; then skip=0; else skip=1; fi
if SKIP_PANEL=$skip sh "$src_dir/contrib/install.sh" >"$log" 2>&1; then
    ok 'built and installed under ~/.local and ~/.config'
else
    say ''
    say 'The build or install failed. Last lines of the log:'
    tail -n 25 "$log"
    say ''
    say "Full log: $log"
    say 'Nothing was enabled, so SteamVR is not affected.'
    exit 1
fi

ctl="$HOME/.local/bin/betterframectl"
if [ "$want_enable" = 1 ]; then
    if "$ctl" enable >/dev/null; then ok 'layer enabled for the next SteamVR start'
    else warn 'could not enable the layer. Run: betterframectl enable'; fi
    if [ "$want_panel" = 1 ]; then
        if [ "$(systemctl --user is-enabled betterframe-xp-panel.service 2>/dev/null)" = enabled ]; then
            ok 'panel enabled: it shows in the SteamVR dashboard from the next start'
        else
            "$ctl" panel-on >/dev/null 2>&1 || true
            if [ "$(systemctl --user is-enabled betterframe-xp-panel.service 2>/dev/null)" = enabled ]; then
                ok 'panel enabled: it shows in the SteamVR dashboard from the next start'
            else
                warn 'panel is NOT enabled, so it will not show in the dashboard. Run: betterframectl panel-on'
            fi
        fi
    fi
fi
rm -f "$log"

# --- what happens next -------------------------------------------------------
say ''
say 'Done. Next:'
if [ "$want_enable" = 1 ]; then
    say '  1. When you are not playing, restart SteamVR (this script does not do it).'
    say '  2. Check:   ~/.local/bin/betterframectl status    -> "loaded in vrcompositor"'
    say '  3. In SteamVR (PC side), turn OFF the encode-time sharpening so it is not applied twice.'
    [ "$want_panel" = 1 ] && say '  4. Tune on the dashboard panel "BetterFrame XP", or: betterframectl set sharpenAmount 1.0'
else
    say '  Enable later with: ~/.local/bin/betterframectl enable   (then restart SteamVR)'
fi
say '  Undo everything: ~/.local/bin/betterframectl uninstall'
exit 0

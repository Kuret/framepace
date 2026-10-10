#!/bin/sh
# framepace installer: builds the Vulkan layer, installs it system-wide with sudo, and sets up the
# fps-cap tool, the session environment and the adaptive-cap user service.
#
#   curl -fsSL https://raw.githubusercontent.com/Kuret/framepace/main/install.sh | sh
#   curl -fsSL https://raw.githubusercontent.com/Kuret/framepace/main/install.sh | sh -s -- --dry-run
#
# Options:
#   --dry-run       print every action, change nothing
#   --no-deps       do not install build dependencies
#   --no-service    do not install/enable the framepace.service user unit
#   --no-env        do not write ~/.config/environment.d/90-framepace.conf
#   --prefix DIR    install the layer under DIR instead of /usr (for testing; the Vulkan loader
#                   only finds the manifest under /usr or /etc, or via VK_ADD_IMPLICIT_LAYER_PATH)
#   -h, --help      this text
#
# Environment:
#   FRAMEPACE_SRC   use this local checkout instead of cloning
#   FRAMEPACE_REF   branch or tag to install when cloning (default: main)
#   FRAMEPACE_REPO  repository URL (default: https://github.com/Kuret/framepace.git)
set -eu

REPO="${FRAMEPACE_REPO:-https://github.com/Kuret/framepace.git}"
REF="${FRAMEPACE_REF:-main}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
CACHE_HOME="${XDG_CACHE_HOME:-$HOME/.cache}"
SRC="${FRAMEPACE_SRC:-$DATA_HOME/framepace/src}"
BUILD="$CACHE_HOME/framepace/build"
BIN_DIR="$HOME/.local/bin"
PREFIX=/usr
DRY_RUN=0
DO_DEPS=1
DO_SERVICE=1
DO_ENV=1

usage() {
    cat <<'EOT'
Usage: install.sh [options]
  --dry-run       print every action, change nothing
  --no-deps       do not install build dependencies
  --no-service    do not install/enable the framepace.service user unit
  --no-env        do not write ~/.config/environment.d/90-framepace.conf
  --prefix DIR    install the layer under DIR instead of /usr (for testing)
  -h, --help      this text
Environment: FRAMEPACE_SRC (local checkout), FRAMEPACE_REF (branch/tag, default main),
             FRAMEPACE_REPO (repository URL)
EOT
}
say() { printf '%s\n' "$*"; }
step() { printf '\n==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# Run a command, or only show it with --dry-run.
run() {
    if [ "$DRY_RUN" = 1 ]; then
        printf '  [dry-run] %s\n' "$*"
        return 0
    fi
    "$@"
}

# write_file PATH [MODE]: content on stdin.
write_file() {
    if [ "$DRY_RUN" = 1 ]; then
        printf '  [dry-run] write %s\n' "$1"
        cat >/dev/null
        return 0
    fi
    mkdir -p "$(dirname "$1")"
    cat >"$1"
    chmod "${2:-644}" "$1"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --no-deps) DO_DEPS=0 ;;
        --no-service) DO_SERVICE=0 ;;
        --no-env) DO_ENV=0 ;;
        --prefix) [ $# -ge 2 ] || die "--prefix needs a directory"; PREFIX="$2"; shift ;;
        --prefix=*) PREFIX="${1#--prefix=}" ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

if [ "$(id -u)" = 0 ] && [ "$DRY_RUN" = 0 ]; then
    die "run this as your normal user, not root; it uses sudo only where needed"
fi

have() { command -v "$1" >/dev/null 2>&1; }

# sudo is only needed when the prefix is not writable by us.
SUDO=sudo
probe="$PREFIX"
while [ ! -e "$probe" ] && [ "$probe" != / ]; do probe="$(dirname "$probe")"; done
if [ -w "$probe" ] || [ "$(id -u)" = 0 ]; then SUDO=; fi

# ---------------------------------------------------------------- dependencies
missing_deps() {
    m=
    have git || m="$m git"
    have meson || m="$m meson"
    have ninja || have ninja-build || m="$m ninja"
    have cc || have gcc || m="$m c-compiler"
    have glslangValidator || m="$m glslangValidator"
    have python3 || m="$m python3"
    [ -f /usr/include/vulkan/vk_layer.h ] || [ -f /usr/local/include/vulkan/vk_layer.h ] ||
        m="$m vulkan-headers"
    printf '%s' "$m"
}

install_deps() {
    step "Build dependencies"
    miss="$(missing_deps)"
    if [ -z "$miss" ]; then
        say "all present (git, meson, ninja, C compiler, Vulkan headers, glslangValidator, python3)"
        return 0
    fi
    say "missing:$miss"
    if have pacman; then
        say "package manager: pacman"
        run sudo pacman -S --needed --noconfirm git meson ninja gcc vulkan-headers glslang python
    elif have apt-get; then
        say "package manager: apt"
        run sudo apt-get update
        run sudo apt-get install -y git meson ninja-build gcc libvulkan-dev glslang-tools python3
    elif have dnf; then
        say "package manager: dnf"
        run sudo dnf install -y git meson ninja-build gcc vulkan-headers glslang python3
    elif have zypper; then
        say "package manager: zypper"
        run sudo zypper --non-interactive install git meson ninja gcc vulkan-headers glslang-devel python3
    else
        if [ "$DRY_RUN" = 1 ]; then
            say "[dry-run] no supported package manager found"
        else
            say "No supported package manager (pacman, apt, dnf, zypper) found."
            say "Install these yourself, then run this script again:"
            say "  git, meson, ninja, a C compiler, Vulkan headers (vulkan/vk_layer.h),"
            say "  glslang (glslangValidator), python3"
            die "missing build dependencies:$miss"
        fi
    fi
}

# ---------------------------------------------------------------------- source
fetch_source() {
    step "Source"
    if [ -n "${FRAMEPACE_SRC:-}" ]; then
        say "using local checkout $SRC"
        [ "$DRY_RUN" = 1 ] || [ -f "$SRC/meson.build" ] || die "$SRC is not a framepace checkout"
    elif [ -d "$SRC/.git" ]; then
        say "updating $SRC to $REF"
        run git -C "$SRC" fetch --depth 1 origin "$REF"
        run git -C "$SRC" checkout -q --detach FETCH_HEAD
    else
        say "cloning $REPO ($REF) into $SRC"
        run mkdir -p "$(dirname "$SRC")"
        run git clone --depth 1 --branch "$REF" "$REPO" "$SRC"
    fi
}

# ----------------------------------------------------------------------- build
build_and_install_layer() {
    step "Build"
    if [ -f "$BUILD/build.ninja" ]; then
        run meson setup --reconfigure "$BUILD" "$SRC" --prefix "$PREFIX" --buildtype release
    else
        run mkdir -p "$(dirname "$BUILD")"
        run meson setup "$BUILD" "$SRC" --prefix "$PREFIX" --buildtype release
    fi
    run meson compile -C "$BUILD"

    step "Install the layer into $PREFIX${SUDO:+ (sudo)}"
    # shellcheck disable=SC2086  # SUDO is intentionally empty or one word
    run $SUDO meson install --no-rebuild -C "$BUILD"
    say "layer:    $PREFIX/lib/framepace/libVkLayer_framepace.so"
    say "manifest: $PREFIX/share/vulkan/implicit_layer.d/framepace.x86_64.json"
    if [ "$PREFIX" != /usr ]; then
        warn "prefix is $PREFIX: the Vulkan loader will not find the manifest there unless you set"
        warn "VK_ADD_IMPLICIT_LAYER_PATH=$PREFIX/share/vulkan/implicit_layer.d"
    fi
}

# ------------------------------------------------------------------- user files
install_tool() {
    step "fps-cap"
    run install -Dm755 "$SRC/tools/fps-cap" "$BIN_DIR/fps-cap"
    case ":$PATH:" in
        *":$BIN_DIR:"*) ;;
        *) warn "$BIN_DIR is not on your PATH; add it (e.g. in ~/.profile) to run fps-cap by name" ;;
    esac
}

write_env() {
    step "Session environment"
    write_file "$CONFIG_HOME/environment.d/90-framepace.conf" <<'EOT'
# Loads the framepace Vulkan layer into every Vulkan app; it only activates in games
# (Steam/Proton/Wine/Lutris/umu environments, or FRAMEPACE_FORCE=1). See the framepace README.
FRAMEPACE=1
EOT
    say "environment file: $CONFIG_HOME/environment.d/90-framepace.conf (read by systemd user sessions at login)"
    say "Without systemd's environment.d, set FRAMEPACE=1 yourself: in ~/.profile or your"
    say "compositor's environment config, or per game in Steam launch options:"
    say "    FRAMEPACE=1 %command%"
}

write_config() {
    step "Configuration"
    cfg="$CONFIG_HOME/framepace/config"
    if [ -e "$cfg" ]; then
        say "keeping existing $cfg"
        return 0
    fi
    write_file "$cfg" <<'EOT'
# framepace / fps-cap configuration. "key = value", '#' starts a comment.

# Lowest cap that fps-cap will set. Put it just above your monitor's VRR floor
# (many panels drop out of VRR below ~48 Hz).
MIN_CAP = 50

# Highest cap. 0 = automatic: monitor refresh - refresh^2/3600 (144 Hz -> 138),
# from the fastest connected output (hyprctl, wlr-randr or xrandr; falls back to 138).
MAX_CAP = 0

# The adaptive cap is 1000 / (tail frame work time * MARGIN).
MARGIN = 1.05

# Hyprland only: pause adapting while the active window is not fullscreen.
PAUSE_WHEN_NOT_FULLSCREEN = yes
EOT
    say "config file: $cfg"
}

install_service() {
    step "Adaptive cap service"
    unit="$CONFIG_HOME/systemd/user/framepace.service"
    write_file "$unit" <<'EOT'
[Unit]
Description=framepace adaptive fps cap daemon

[Service]
ExecStart=%h/.local/bin/fps-cap daemon
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
EOT
    if have systemctl; then
        run systemctl --user daemon-reload
        run systemctl --user enable --now framepace.service ||
            warn "could not enable the service (no systemd user session?); run 'fps-cap daemon' from your autostart instead"
    else
        warn "systemctl not found: start 'fps-cap daemon' from your session autostart instead"
    fi
    say "If you already start 'fps-cap daemon' from a compositor autostart, remove that line: only one daemon runs."
}

next_steps() {
    cat <<'EOT'

==> Done. Next steps

1. Log out and back in once, so FRAMEPACE=1 reaches your session (or set it yourself, see above).
2. Start a game (Steam/Proton, Heroic, Lutris, Bottles, umu). The overlay appears at the top left.
   Native games not launched through Steam need FRAMEPACE_FORCE=1 as well.
3. Check:   fps-cap status          (frames counter increases while a game runs)
            FRAMEPACE_DEBUG=1 <game>  logs why the layer is active or not
4. Hotkeys: bind keys to these commands.
   Hyprland (hyprland.conf):
       bind = , F9,  exec, fps-cap down
       bind = , F10, exec, fps-cap up
       bind = , F11, exec, fps-cap adaptive toggle
       bind = , F12, exec, fps-cap overlay toggle
   Sway (config):
       bindsym F9 exec fps-cap down
       bindsym F10 exec fps-cap up
       bindsym F11 exec fps-cap adaptive toggle
       bindsym F12 exec fps-cap overlay toggle
   KDE: System Settings > Shortcuts > Add Command; GNOME: Settings > Keyboard > Custom Shortcuts.
   Use the full path ~/.local/bin/fps-cap there if your desktop does not have it on PATH.
Edit ~/.config/framepace/config to tune MIN_CAP, MAX_CAP and MARGIN. Uninstall: uninstall.sh.
EOT
}

[ "$DRY_RUN" = 1 ] && say "framepace install (dry run: nothing will be changed)"
if [ "$DO_DEPS" = 1 ]; then install_deps; else step "Build dependencies"; say "skipped (--no-deps)"; fi
fetch_source
build_and_install_layer
install_tool
if [ "$DO_ENV" = 1 ]; then write_env; fi
write_config
if [ "$DO_SERVICE" = 1 ]; then install_service; fi
next_steps

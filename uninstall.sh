#!/bin/sh
# Removes what install.sh set up: the layer and manifest (sudo), the fps-cap tool, the
# framepace.service user unit, the environment.d file, the cached source and build.
# Your ~/.config/framepace (config and per-game settings) is kept unless you pass --purge.
#
#   curl -fsSL https://raw.githubusercontent.com/Kuret/framepace/main/uninstall.sh | sh
#
# Options:
#   --dry-run     print every action, change nothing
#   --prefix DIR  the prefix the layer was installed into (default /usr)
#   --purge       also remove the config, the per-game settings (games/) and the log
#   -h, --help    this text
set -eu

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
CACHE_HOME="${XDG_CACHE_HOME:-$HOME/.cache}"
STATE_DIR="$HOME/.local/state/framepace"      # run files live here
USER_CONFIG="$HOME/.config/framepace"         # config and games/*.conf
PREFIX=/usr
DRY_RUN=0
PURGE=0

usage() {
    cat <<'EOT'
Usage: uninstall.sh [--dry-run] [--prefix DIR] [--purge]
  --dry-run     print every action, change nothing
  --prefix DIR  the prefix the layer was installed into (default /usr)
  --purge       also remove the config, the per-game settings (games/) and the log
EOT
}
say() { printf '%s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

run() {
    if [ "$DRY_RUN" = 1 ]; then
        printf '  [dry-run] %s\n' "$*"
        return 0
    fi
    "$@"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1 ;;
        --purge) PURGE=1 ;;
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

SUDO=sudo
probe="$PREFIX/lib/framepace"
while [ ! -e "$probe" ] && [ "$probe" != / ]; do probe="$(dirname "$probe")"; done
if [ -w "$probe" ] || [ "$(id -u)" = 0 ]; then SUDO=; fi

# rm_path PATH...: remove files/dirs that exist
rm_user() {
    for p in "$@"; do
        if [ -e "$p" ] || [ -L "$p" ]; then
            say "removing $p"
            run rm -rf "$p"
        fi
    done
}

[ "$DRY_RUN" = 1 ] && say "framepace uninstall (dry run: nothing will be changed)"

if have systemctl && [ -e "$CONFIG_HOME/systemd/user/framepace.service" ]; then
    say "stopping framepace.service"
    run systemctl --user disable --now framepace.service || true
fi
rm_user "$CONFIG_HOME/systemd/user/framepace.service"
if have systemctl && [ "$DRY_RUN" = 0 ]; then systemctl --user daemon-reload 2>/dev/null || true; fi

rm_user "$CONFIG_HOME/environment.d/90-framepace.conf" "$HOME/.local/bin/fps-cap"
rm_user "$DATA_HOME/framepace" "$CACHE_HOME/framepace/build"
# live state: per-process run files and the old single state file
rm_user "$STATE_DIR/run" "$STATE_DIR/state"

lib="$PREFIX/lib/framepace"
manifest="$PREFIX/share/vulkan/implicit_layer.d/framepace.x86_64.json"
if [ -e "$lib" ] || [ -e "$manifest" ]; then
    say "removing $lib and $manifest${SUDO:+ (sudo)}"
    # shellcheck disable=SC2086  # SUDO is intentionally empty or one word
    run $SUDO rm -rf "$lib" "$manifest"
fi

if [ "$PURGE" = 1 ]; then
    rm_user "$USER_CONFIG" "$STATE_DIR" "$CACHE_HOME/framepace" "$CACHE_HOME/fps-cap.log"
else
    [ -e "$USER_CONFIG" ] && say "kept $USER_CONFIG (config and per-game settings; use --purge to remove it)"
fi
[ "$DRY_RUN" = 1 ] || rmdir "$STATE_DIR" 2>/dev/null || true

say "done. Log out and in again to drop FRAMEPACE=1 from your session environment."

#!/bin/sh
# One-time cleanup of the pre-rename install ("frame-post-fx" -> "betterframe-xp").
# Called by install.sh before it installs the new files. Does nothing if no old install exists.
# User space only, no sudo. Never restarts SteamVR (a running vrcompositor keeps the old layer
# mapped until the user restarts SteamVR; the next start uses the new names).
#   - old files are moved to ~/betterframe-xp-before-rename/ (rollback = copy them back)
#   - params.conf and gaze-before.conf are carried over to the new config dir
#   - if the old layer was enabled, the new one is enabled too (via betterframectl enable, in install.sh)
# Can be deleted from a public release (nobody else has the old names).
set -u

old_conf="$HOME/.config/frame-post-fx"
old_data="$HOME/.local/share/frame-post-fx"
old_unit="$HOME/.config/systemd/user/frame-post-fx-panel.service"
old_dropin="$HOME/.config/systemd/user/steamvr.service.d/91-frame-post-fx.conf"
old_manifest="$HOME/.local/share/vulkan/implicit_layer.d/frame-post-fx.json"
old_bin="$HOME/.local/bin/framepostfxctl"
old_panel="$HOME/.local/bin/frame-post-fx-panel"
old_guard="$HOME/.local/libexec/frame-post-fx-guard"
new_conf="$HOME/.config/betterframe-xp"
backup="$HOME/betterframe-xp-before-rename"
state="${XDG_RUNTIME_DIR:-/tmp}/betterframe-xp-migrated-enabled"

found=0
for p in "$old_conf" "$old_data" "$old_unit" "$old_dropin" "$old_manifest" "$old_bin" "$old_panel" "$old_guard"; do
    [ -e "$p" ] && found=1
done
[ "$found" = 1 ] || exit 0

echo "migrating from frame-post-fx (backup: $backup)"
mkdir -p "$backup" "$new_conf"

# Remember whether the old layer was enabled (drop-in present and not turned off by the guard).
rm -f "$state"
if [ -f "$old_dropin" ] && [ ! -f "$old_conf/guard-disabled" ]; then : > "$state"; fi

# Stop the old panel first so two dashboard panels never exist at once.
systemctl --user disable --now frame-post-fx-panel.service >/dev/null 2>&1 || true

# Carry the settings over (never overwrite what the new install already has).
for f in params.conf gaze-before.conf; do
    if [ -f "$old_conf/$f" ] && [ ! -f "$new_conf/$f" ]; then cp -p "$old_conf/$f" "$new_conf/$f"; fi
done

# Back up, then remove the old files.
[ -d "$old_conf" ] && cp -a "$old_conf" "$backup/config" 2>/dev/null
[ -d "$old_data" ] && cp -a "$old_data" "$backup/share" 2>/dev/null
for p in "$old_unit" "$old_dropin" "$old_manifest" "$old_bin" "$old_panel" "$old_guard"; do
    [ -e "$p" ] && cp -p "$p" "$backup/" 2>/dev/null
done
rm -f "$old_unit" "$old_dropin" "$old_manifest" "$old_bin" "$old_panel" "$old_guard" \
      "$HOME/.config/systemd/user/steamvr.service.wants/frame-post-fx-panel.service" \
      "$HOME/.local/share/vulkan/implicit_layer.d/frame-post-fx-vrlink.json"
rm -rf "$old_conf" "$old_data"
rm -rf "${XDG_RUNTIME_DIR:-/tmp}/frame-post-fx-guard" "${XDG_RUNTIME_DIR:-/tmp}/frame-post-fx-panel.lock"
systemctl --user daemon-reload >/dev/null 2>&1 || true
echo 'old frame-post-fx files removed (the layer already loaded in a running vrcompositor stays until SteamVR restarts)'
exit 0

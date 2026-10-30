#!/usr/bin/env bash
# Headless Blender + blender-mcp socket server (localhost:9876). No GUI window.
# Usage: art/blender-mcp-server.sh [file.blend]   (PORT=9876 env to override)
# Only PolyHaven (CC0) is enabled; Hyper3D/Hunyuan/Sketchfab/Tripo/Meshy/PolyPizza forced off.
# Client: art/blender-mcp-cmd.py <command> ['{json params}']
set -euo pipefail
BLENDER="${BLENDER:-$HOME/.local/opt/blender/blender}"
export BMCP_PORT="${PORT:-9876}"
BOOT=$(mktemp --suffix=.py)
cat > "$BOOT" <<'PY'
import bpy, os, socket, threading, time, addon_utils
addon_utils.enable("blender_mcp", default_set=False, persistent=True)
import blender_mcp as m
sc = bpy.context.scene
for k in ("hyper3d","hunyuan3d","sketchfab","tripo","meshy","polypizza"):
    if hasattr(sc, "blendermcp_use_"+k): setattr(sc, "blendermcp_use_"+k, False)
sc.blendermcp_use_polyhaven = True
port = int(os.environ["BMCP_PORT"])
s = m.BlenderMCPServer(port=port)
# addon.start() refuses -b because it relies on GUI timers; do the same setup and drain the queue ourselves.
s.running = True
s.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.socket.bind(("localhost", port)); s.socket.listen(5)
s.server_thread = threading.Thread(target=s._server_loop, daemon=True); s.server_thread.start()
print(f"BlenderMCP headless server on localhost:{port}", flush=True)
try:
    while s.running:
        s._drain_command_queue(); time.sleep(0.02)
except KeyboardInterrupt:
    pass
s.stop()
PY
trap 'rm -f "$BOOT"' EXIT
exec "$BLENDER" -b "$@" --python "$BOOT"

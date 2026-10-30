#!/usr/bin/env python3
"""Send one command to the blender-mcp addon socket and print the JSON reply.
Usage: blender-mcp-cmd.py get_scene_info
       blender-mcp-cmd.py execute_code '{"code":"import bpy; print(len(bpy.data.objects))"}'
"""
import json, os, socket, sys
cmd = sys.argv[1]; params = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
s = socket.create_connection(("localhost", int(os.environ.get("PORT", 9876))), timeout=300)
s.sendall(json.dumps({"type": cmd, "params": params}).encode())
buf = b""
while True:
    chunk = s.recv(65536)
    if not chunk: break
    buf += chunk
    try: json.loads(buf); break
    except ValueError: continue
print(json.dumps(json.loads(buf), indent=2))

#!/usr/bin/env bash
# Regenerate the repo-root manifest.json (used by `omarchy plugin add <repo-url>`) from plugin/manifest.json.
cd "$(dirname "$0")/.." && python3 -c '
import json; m=json.load(open("plugin/manifest.json"))
m["entryPoints"]={k:"plugin/"+v for k,v in m["entryPoints"].items()}
open("manifest.json","w").write(json.dumps(m,indent=2)+"\n")'

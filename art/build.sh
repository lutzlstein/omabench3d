#!/usr/bin/env bash
# Asset pipeline: Blender generators -> art/export/*.fbx -> UE uassets (/Game/OmaBench/Meshes). Then package as usual.
set -euo pipefail
cd "$(dirname "$0")/.."
BL="${BLENDER:-blender}"; UE="${UE_ROOT:-$HOME/UnrealEngine/5.8.4}"
"$BL" -b --python art/blender/vehicles.py
"$BL" -b --python art/blender/rooftop.py
"$UE/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PWD/omabench.uproject" -run=pythonscript -script="$PWD/Scripts/import_blender_assets.py" -unattended -nosplash -nullrhi
[ "${1:-}" = "--renders" ] && for a in SM_Spinner SM_CarA SM_CarB; do "$BL" -b --python art/blender/render_turntable.py -- "$a" 8; done
echo "art pipeline done"

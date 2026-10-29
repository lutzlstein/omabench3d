#!/usr/bin/env bash
# Asset pipeline: Blender generators (in parallel) -> art/export/*.fbx -> UE uassets, T3 materials,
# then the OmaFx commandlet builds the T3 Niagara systems. Two editor boots total. Then package as usual.
set -euo pipefail
cd "$(dirname "$0")/.."
BL="${BLENDER:-blender}"; UE="${UE_ROOT:-$HOME/UnrealEngine/5.8.4}"; CMD="$UE/Engine/Binaries/Linux/UnrealEditor-Cmd"
mkdir -p art/export; pids=()
for g in vehicles rooftop market; do "$BL" -b --python "art/blender/$g.py" > "art/export/$g.log" 2>&1 & pids+=($!); done
for p in "${pids[@]}"; do wait "$p" || { echo "blender generator failed (see art/export/*.log)"; exit 1; }; done
"$CMD" "$PWD/omabench.uproject" -run=pythonscript -script="$PWD/Scripts/art_pipeline.py" -unattended -nosplash -nullrhi
rm -f Content/OmaBench/Fx/NS_*.uasset   # regenerated from code each time (stale packages break re-save)
"$CMD" "$PWD/omabench.uproject" -run=OmaFx -unattended -nosplash -nullrhi
[ "${1:-}" = "--renders" ] && for a in SM_Spinner SM_CarA SM_CarB; do "$BL" -b --python art/blender/render_turntable.py -- "$a" 8; done
echo "art pipeline done"

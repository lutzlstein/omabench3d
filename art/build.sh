#!/usr/bin/env bash
# Asset pipeline: fetch CC0 sources -> Blender generators (in parallel) -> CC0 kit (replaces vehicles/props with
# CC0 assets, art/THIRD_PARTY.md) -> art/export/*.fbx -> UE uassets + CC0 materials/LODs, T3 materials,
# then the OmaFx commandlet builds the T3 Niagara systems. Two editor boots total. Then package as usual.
set -euo pipefail
cd "$(dirname "$0")/.."
BL="${BLENDER:-blender}"; UE="${UE_ROOT:-$HOME/UnrealEngine/5.8.4}"; CMD="$UE/Engine/Binaries/Linux/UnrealEditor-Cmd"
tools/fetch-cc0.sh
mkdir -p art/export; pids=()
for g in vehicles rooftop market; do "$BL" -b --python "art/blender/$g.py" > "art/export/$g.log" 2>&1 & pids+=($!); done
for p in "${pids[@]}"; do wait "$p" || { echo "blender generator failed (see art/export/*.log)"; exit 1; }; done
"$BL" -b --python art/blender/cc0_kit.py > art/export/cc0_kit.log 2>&1 || { echo "cc0 kit failed (see art/export/cc0_kit.log)"; exit 1; }
"$CMD" "$PWD/omabench.uproject" -run=pythonscript -script="$PWD/Scripts/art_pipeline.py" -unattended -nosplash -nullrhi
rm -f Content/OmaBench/Fx/NS_*.uasset   # regenerated from code each time (stale packages break re-save)
"$CMD" "$PWD/omabench.uproject" -run=OmaFx -unattended -nosplash -nullrhi
[ "${1:-}" = "--renders" ] && for a in SM_Spinner SM_CarA SM_CarB; do "$BL" -b --python art/blender/render_turntable.py -- "$a" 8; done
echo "art pipeline done"

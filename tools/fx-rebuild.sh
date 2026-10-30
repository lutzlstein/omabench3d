#!/usr/bin/env bash
# Rebuild only the T3 particle materials and Niagara systems (no Blender/mesh import): editor build,
# make_t3_materials.py, then the OmaFx commandlet. Use after editing either; then tools/package.sh.
set -euo pipefail
cd "$(dirname "$0")/.."; UE="${UE_ROOT:-$HOME/UnrealEngine/5.8.4}"; CMD="$UE/Engine/Binaries/Linux/UnrealEditor-Cmd"
"$UE/Engine/Build/BatchFiles/Linux/Build.sh" OmaBenchEditor Linux Development -project="$PWD/omabench.uproject" > fx-rebuild.log 2>&1
"$CMD" "$PWD/omabench.uproject" -run=pythonscript -script="$PWD/Scripts/make_t3_materials.py" -unattended -nosplash -nullrhi >> fx-rebuild.log 2>&1
grep -q "Python script executed successfully" fx-rebuild.log || { echo "materials failed (fx-rebuild.log)"; exit 1; }
rm -f Content/OmaBench/Fx/NS_*.uasset
"$CMD" "$PWD/omabench.uproject" -run=OmaFx -unattended -nosplash -nullrhi >> fx-rebuild.log 2>&1
grep 'OMAFX .* built' fx-rebuild.log

#!/usr/bin/env bash
# tierrun.sh TESTS RES TIER [profile=1]: one -quick windowed run on DP-3 at a tier; prints scores (+ GPU profile tree).
cd ~/Projects/omabench
L=~/.local/share/omabench3d/last-run.log
OMABENCH3D_PROFILEGPU="${4:-0}" plugin/bin/omabench3d-ctl start "$PWD/Packaged/Linux/OmaBench.sh" "$1" "$2" 1 0 1 3 20 DP-3 auto 1 omarchy 1 "$3" >/dev/null
sleep 5
while pgrep -x OmaBench >/dev/null; do sleep 2; done
grep -a 'score=\|slow frames\|tier ' "$L" | sed 's/.*Display: //;s/.*Warning: //'
[[ "${4:-0}" == 1 ]] && python3 tools/gpuprofile.py "$L" 0.8 9

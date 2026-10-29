#!/usr/bin/env bash
# quickrun.sh TESTS [extra-label]: one -quick windowed run on DP-3 via the panel's ctl; waits, then prints
# EXTRA="auto 0 omarchy 0" appends pixel/hud/theme/sysmon (e.g. overhead profiling: hud and sysmon off).
# per-test score/fps, startup (process start -> first warmup frame) and the results path.
set -euo pipefail
cd "$(dirname "$0")/.."
T="${1:-T1,T2,T3}"; W="${WARM:-3}"; D="${DUR:-20}"
P=~/.local/share/omabench3d/results/progress.json; L=~/.local/share/omabench3d/last-run.log
while pgrep -x OmaBench >/dev/null; do sleep 1; done
R=$(plugin/bin/omabench3d-ctl start "$PWD/Packaged/Linux/OmaBench.sh" "$T" 1920x1080 1 0 1 "$W" "$D" DP-3 ${EXTRA:-} | tail -1)
until grep -q "\"$R\"" "$P" 2>/dev/null && grep -q '"state": "\(done\|aborted\|failed\)' "$P"; do sleep 1; done
while pgrep -x OmaBench >/dev/null; do sleep 1; done
grep -a 'score=' "$L" | sed 's/.*OmaBench //'
awk -F'[][]' '/^\[20/ { split($2,a,/[-.:]/); t=a[4]*3600+a[5]*60+a[6]+a[7]/1000; if(!s) s=t } /RT profile applied/ && !w { w=t } END { if (w) printf "startup_s=%.2f\n", w-s }' "$L"

#!/usr/bin/env bash
# Automated camera-path clearance test: fails (exit 3) if any test's camera comes within ${1:-150} cm of static geometry.
# Runs the packaged build offscreen (no window, no screen capture).
set -u; cd "$(dirname "$0")/.."
LOG=Saved/campathcheck.log
timeout 600 Packaged/Linux/OmaBench.sh -RenderOffscreen -nosound -tests=${TESTS:-T1,T2,T3} -res=640x360 -windowed -tier=light -nosysmon -campathcheck=${1:-150} ${CAMTRACE:+-camtrace} > "$LOG" 2>&1; rc=$?
grep -a "CAMCHECK" "$LOG" | sed 's/.*LogCamCheck: Display: //'
exit $rc

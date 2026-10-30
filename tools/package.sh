#!/usr/bin/env bash
# Package OmaBench (Linux, Development) into Packaged/. Iterative cook, no debug-info files, and the
# Vulkan debug/capture layers (validation, api_dump, gfxreconstruct, crash_diagnostic) pruned from the
# archive: they are only loaded with -vulkandebug and are ~263 MB.
set -euo pipefail
cd "$(dirname "$0")/.."; UE="${UE_ROOT:-$HOME/UnrealEngine/5.8.4}"; t0=$(date +%s)
nice -n 19 ionice -c3 "$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PWD/omabench.uproject" -platform=Linux -clientconfig=Development \
  -build -cook -iterate -stage -pak -iostore -archive -archivedirectory="$PWD/Packaged" -nodebuginfo -nop4 -utf8output -unattended > package.log 2>&1 \
  || { grep -E ': error' package.log | head; exit 1; }
V=Packaged/Linux/Engine/Binaries/ThirdParty/Vulkan/Linux; for l in api_dump gfxreconstruct khronos_validation crash_diagnostic khronos_profiles; do rm -f "$V/libVkLayer_$l.so" "$V/VkLayer_$l.json"; done
rm -f \
      Packaged/Linux/omabench/Binaries/Linux/OmaBench.debug Packaged/Linux/omabench/Binaries/Linux/OmaBench.sym
echo "packaged in $(( $(date +%s) - t0 ))s, size $(du -sh Packaged/Linux | cut -f1)"

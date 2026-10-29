#!/usr/bin/env bash
# Build the release tarball the plugin's `omabench3d-ctl install-app` downloads:
#   dist/omabench3d-linux-x86_64.tar.zst + .sha256  (contents of Packaged/Linux, OmaBench.sh at the root)
# Does NOT create or upload a GitHub release. Upload (after review):
#   gh release create vX.Y.Z dist/omabench3d-linux-x86_64.tar.zst dist/omabench3d-linux-x86_64.tar.zst.sha256
set -euo pipefail
cd "$(dirname "$0")/.."
[[ "${1:-}" == --no-package ]] || tools/package.sh
[[ -x Packaged/Linux/OmaBench.sh ]] || { echo "no Packaged/Linux/OmaBench.sh" >&2; exit 1; }
mkdir -p dist; out=dist/omabench3d-linux-x86_64.tar.zst
tar -C Packaged/Linux --owner=0 --group=0 --sort=name -cf - . | zstd -T0 -19 --long=27 -q -o "$out" -f
(cd dist && sha256sum "$(basename "$out")" > "$(basename "$out").sha256")
echo "$(du -h "$out" | cut -f1)  $out"; cat "$out.sha256"

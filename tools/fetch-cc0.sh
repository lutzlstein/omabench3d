#!/usr/bin/env bash
# Download the CC0 source assets listed in art/cc0/assets.txt into art/cc0/src (gitignored; reproducible).
# Poly Haven models (glTF, 1k textures), ambientCG materials (1K JPG), OpenGameArt zips. All CC0 1.0,
# see art/THIRD_PARTY.md. Re-running skips what is already there.
set -euo pipefail
cd "$(dirname "$0")/.."
S=art/cc0/src; mkdir -p "$S"
while read -r kind id name; do
  [[ -z "$kind" || "$kind" == \#* ]] && continue
  case "$kind" in
    ph) d="$S/ph/$id"; [[ -f "$d/.alpha" ]] && continue; mkdir -p "$d/textures"
        j="$(curl -fsS "https://api.polyhaven.com/files/$id")"
        # opacity/alpha (and some diffuse) maps are not part of the glTF bundle (alpha-tested wire, grilles)
        jq -r 'to_entries[] | select(.key|test("opac|alpha|diff";"i")) | .value["1k"].jpg.url' <<<"$j" | while read -r url; do
          f="$d/textures/$(basename "$url")"; [[ -f "$f" ]] || curl -fsSL -o "$f" "$url"; done; touch "$d/.alpha"
        [[ -f "$d/$id.gltf" ]] && continue
        curl -fsSL -o "$d/$id.gltf" "$(jq -r '.gltf["1k"].gltf.url' <<<"$j")"
        jq -r '.gltf["1k"].gltf.include | to_entries[] | "\(.key)\t\(.value.url)"' <<<"$j" | while IFS=$'\t' read -r rel url; do
          mkdir -p "$d/$(dirname "$rel")"; curl -fsSL -o "$d/$rel" "$url"; done ;;
    acg) d="$S/acg/$id"; [[ -d "$d" ]] && continue; mkdir -p "$d"
        curl -fsSL -o "$d.zip" "https://ambientcg.com/get?file=${id}_1K-JPG.zip"; (cd "$d" && unzip -q "../$id.zip"); rm "$d.zip" ;;
    oga) n="${name:-$(basename "$id" .zip)}"; d="$S/oga/$n"; [[ -d "$d" ]] && continue; mkdir -p "$d"
        curl -fsSL -o "$d.zip" "$id"; (cd "$d" && unzip -q "../$n.zip"); rm "$d.zip" ;;
  esac
  echo "fetched $kind $id"
done < art/cc0/assets.txt

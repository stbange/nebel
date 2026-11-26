#!/usr/bin/env bash
# Optional 1-click installer for Depth Anything 3 Monocular Large (DA3Mono-Large, Apache-2.0, ~1.34 GB)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
MODEL_DIR="$ROOT_DIR/assets/models"
OUT_FILE="$MODEL_DIR/da3_mono_large.onnx"
URL="https://huggingface.co/yuvraj108c/Depth-Anything-3-Onnx/resolve/main/DA3Mono-Large.onnx"

mkdir -p "$MODEL_DIR"
echo "Downloading DA3Mono-Large (Apache-2.0, ~1.34 GB) into $OUT_FILE ..."
curl -L --fail --progress-bar "$URL" -o "$OUT_FILE.part"
mv "$OUT_FILE.part" "$OUT_FILE"
echo "Done! Restart Nebel (or click HD 3D / Render) to use DA3Mono-Large."

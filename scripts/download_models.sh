#!/usr/bin/env bash
#
# Download ONNX models from HuggingFace Hub according to data/models/models.json.
# Dependencies: curl, sha256sum, python3 (for JSON parsing)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFEST="$PROJECT_ROOT/data/models/models.json"

TARGET_DIR=""
MAX_RETRIES=3

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Download ONNX model files from HuggingFace Hub.

Options:
  --target-dir DIR   Output directory for models (default: data/models/)
  --manifest FILE    Path to models.json manifest (default: auto-detected)
  -h, --help         Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target-dir)  TARGET_DIR="$2"; shift 2 ;;
        --manifest)    MANIFEST="$2"; shift 2 ;;
        -h|--help)     usage; exit 0 ;;
        *)             echo "Unknown option: $1" >&2; usage; exit 1 ;;
    esac
done

if [[ ! -f "$MANIFEST" ]]; then
    echo "ERROR: Manifest not found: $MANIFEST" >&2
    exit 1
fi

if [[ -z "$TARGET_DIR" ]]; then
    TARGET_DIR="$PROJECT_ROOT/data/models"
fi
mkdir -p "$TARGET_DIR"

# Parse the entire manifest once into a tab-separated list:
#   base_url \t path \t sha256 \t size_bytes   (one line per model)
PARSED=$(python3 -c "
import json, sys
m = json.load(sys.stdin)
base = m['base_url']
for e in m['models']:
    print(f\"{base}\t{e['path']}\t{e['sha256']}\t{e['size_bytes']}\")
" < "$MANIFEST")

MODEL_COUNT=$(echo "$PARSED" | wc -l)

echo "Manifest : $MANIFEST"
echo "Target   : $TARGET_DIR"
echo "Models   : $MODEL_COUNT"
echo "---"

FAILED=0

while IFS=$'\t' read -r BASE_URL MODEL_PATH EXPECTED_SHA EXPECTED_SIZE; do
    URL="$BASE_URL/$MODEL_PATH"
    DEST="$TARGET_DIR/$MODEL_PATH"
    mkdir -p "$(dirname "$DEST")"

    if [[ -f "$DEST" ]]; then
        ACTUAL_SHA=$(sha256sum "$DEST" | awk '{print $1}')
        if [[ "$ACTUAL_SHA" == "$EXPECTED_SHA" ]]; then
            echo "SKIP  $MODEL_PATH  (sha256 OK)"
            continue
        fi
        echo "STALE $MODEL_PATH  (sha256 mismatch, re-downloading)"
    fi

    SUCCESS=false
    for attempt in $(seq 1 "$MAX_RETRIES"); do
        echo "GET   $MODEL_PATH  (attempt $attempt/$MAX_RETRIES, ${EXPECTED_SIZE} bytes)"
        if curl -fSL --progress-bar -o "$DEST.tmp" "$URL"; then
            ACTUAL_SHA=$(sha256sum "$DEST.tmp" | awk '{print $1}')
            if [[ "$ACTUAL_SHA" == "$EXPECTED_SHA" ]]; then
                mv "$DEST.tmp" "$DEST"
                echo "  OK  sha256=$ACTUAL_SHA"
                SUCCESS=true
                break
            else
                echo "  FAIL sha256 mismatch: expected=$EXPECTED_SHA actual=$ACTUAL_SHA" >&2
                rm -f "$DEST.tmp"
            fi
        else
            echo "  FAIL curl error (exit $?)" >&2
            rm -f "$DEST.tmp"
        fi
    done

    if [[ "$SUCCESS" != "true" ]]; then
        echo "ERROR: Failed to download $MODEL_PATH after $MAX_RETRIES attempts" >&2
        FAILED=$((FAILED + 1))
    fi
done <<< "$PARSED"

echo "---"
if [[ $FAILED -gt 0 ]]; then
    echo "DONE with $FAILED error(s)." >&2
    exit 1
fi
echo "All models downloaded successfully."

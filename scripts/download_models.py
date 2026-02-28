#!/usr/bin/env python3
"""
Download ONNX models from HuggingFace Hub according to data/models/models.json.
Cross-platform replacement for download_models.sh (works on Linux, macOS, Windows).

Dependencies: Python 3.8+ stdlib only (no pip installs required).
"""

import argparse
import hashlib
import json
import shutil
import sys
import time
import urllib.request
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_MANIFEST = PROJECT_ROOT / "data" / "models" / "models.json"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def download_with_retry(url: str, dest: Path, max_retries: int = 3) -> bool:
    tmp = dest.with_suffix(dest.suffix + ".tmp")
    for attempt in range(1, max_retries + 1):
        try:
            print(f"  GET {url}  (attempt {attempt}/{max_retries})")
            with urllib.request.urlopen(url, timeout=120) as resp, tmp.open("wb") as out:
                shutil.copyfileobj(resp, out)
            return True
        except Exception as exc:
            print(f"  FAIL {exc}", file=sys.stderr)
            tmp.unlink(missing_ok=True)
            if attempt < max_retries:
                time.sleep(2)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Download ONNX models from HuggingFace Hub.")
    parser.add_argument("--target-dir", type=Path, default=None,
                        help="Output directory for models (default: data/models/)")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST,
                        help="Path to models.json manifest")
    parser.add_argument("--retries", type=int, default=3,
                        help="Download retry count per file (default: 3)")
    args = parser.parse_args()

    manifest_path: Path = args.manifest
    if not manifest_path.is_file():
        print(f"ERROR: Manifest not found: {manifest_path}", file=sys.stderr)
        return 1

    with manifest_path.open(encoding="utf-8") as f:
        manifest = json.load(f)

    base_url: str = manifest["base_url"].rstrip("/")
    models: list = manifest["models"]
    target_dir: Path = args.target_dir or PROJECT_ROOT / "data" / "models"
    target_dir.mkdir(parents=True, exist_ok=True)

    print(f"Manifest : {manifest_path}")
    print(f"Target   : {target_dir}")
    print(f"Models   : {len(models)}")
    print("---")

    failed = 0
    for entry in models:
        rel_path: str = entry["path"]
        expected_sha: str = entry["sha256"]
        size_bytes: int = entry.get("size_bytes", 0)
        dest = target_dir / rel_path
        dest.parent.mkdir(parents=True, exist_ok=True)

        if dest.is_file():
            if sha256_file(dest) == expected_sha:
                print(f"SKIP  {rel_path}  (sha256 OK)")
                continue
            print(f"STALE {rel_path}  (sha256 mismatch, re-downloading)")

        url = f"{base_url}/{rel_path}"
        print(f"GET   {rel_path}  ({size_bytes} bytes)")
        tmp = dest.with_suffix(dest.suffix + ".tmp")

        if not download_with_retry(url, dest, max_retries=args.retries):
            print(f"ERROR: Failed to download {rel_path} after {args.retries} attempts",
                  file=sys.stderr)
            failed += 1
            continue

        actual_sha = sha256_file(tmp)
        if actual_sha != expected_sha:
            print(f"  FAIL sha256 mismatch: expected={expected_sha} actual={actual_sha}",
                  file=sys.stderr)
            tmp.unlink(missing_ok=True)
            failed += 1
            continue

        tmp.rename(dest)
        print(f"  OK  sha256={actual_sha}")

    print("---")
    if failed:
        print(f"DONE with {failed} error(s).", file=sys.stderr)
        return 1
    print("All models downloaded successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

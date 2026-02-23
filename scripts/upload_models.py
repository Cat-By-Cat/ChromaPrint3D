#!/usr/bin/env python3
"""Upload ONNX model files to HuggingFace Hub based on models.json manifest."""

import argparse
import hashlib
import json
import sys
from pathlib import Path

try:
    from huggingface_hub import HfApi
except ImportError:
    print("Error: huggingface_hub is required.\n  pip install huggingface_hub", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
MANIFEST_PATH = PROJECT_ROOT / "data" / "models" / "models.json"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_manifest() -> dict:
    with open(MANIFEST_PATH) as f:
        return json.load(f)


def save_manifest(manifest: dict) -> None:
    with open(MANIFEST_PATH, "w") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")


def get_remote_sha256(api: HfApi, repo_id: str, path: str) -> str | None:
    """Return the SHA256 of an LFS file on HuggingFace, or None if not found."""
    try:
        info_list = api.get_paths_info(repo_id, paths=[path], repo_type="model")
        for info in info_list:
            if hasattr(info, "lfs") and info.lfs:
                return info.lfs.sha256
        return None
    except Exception:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--create-repo",
        action="store_true",
        help="Create the HuggingFace repository if it does not exist",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Upload all files regardless of remote state",
    )
    parser.add_argument(
        "--models-dir",
        type=Path,
        default=PROJECT_ROOT / "data" / "models",
        help="Local directory containing model files (default: data/models/)",
    )
    args = parser.parse_args()

    manifest = load_manifest()
    repo_id = manifest["repository"]
    api = HfApi()

    if args.create_repo:
        api.create_repo(repo_id, repo_type="model", exist_ok=True)
        print(f"Repository '{repo_id}' is ready.")

    updated = False
    for entry in manifest["models"]:
        local_path = args.models_dir / entry["path"]
        if not local_path.exists():
            print(f"SKIP  {entry['path']}  (file not found at {local_path})")
            continue

        local_sha = sha256_file(local_path)

        if not args.force:
            remote_sha = get_remote_sha256(api, repo_id, entry["path"])
            if remote_sha and remote_sha == local_sha:
                print(f"SKIP  {entry['path']}  (remote sha256 matches)")
                continue

        size_mb = local_path.stat().st_size / 1e6
        print(f"UPLOAD {entry['path']}  ({size_mb:.1f} MB) ...")
        api.upload_file(
            path_or_fileobj=str(local_path),
            path_in_repo=entry["path"],
            repo_id=repo_id,
            repo_type="model",
        )

        entry["sha256"] = local_sha
        entry["size_bytes"] = local_path.stat().st_size
        updated = True
        print(f"  OK   sha256={local_sha}")

    if updated:
        save_manifest(manifest)
        print(f"\nManifest updated: {MANIFEST_PATH}")
    else:
        print("\nNothing to upload.")


if __name__ == "__main__":
    main()

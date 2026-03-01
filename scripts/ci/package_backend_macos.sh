#!/usr/bin/env bash
set -euo pipefail

mkdir -p pkg/lib
cp build/bin/chromaprint3d_server pkg/
find build/_deps/onnxruntime-src/lib -name "libonnxruntime*.dylib*" \
  -exec cp {} pkg/lib/ \; 2>/dev/null || true

export BREW_PREFIX="$(brew --prefix)"
export OPENCV_PREFIX="$(brew --prefix opencv)"
export LIBOMP_PREFIX="$(brew --prefix libomp)"
export JPEG_PREFIX="$(brew --prefix jpeg-turbo)"

python3 - <<'PY'
import os
import shutil
import subprocess
from pathlib import Path

pkg = Path("pkg")
lib_dir = pkg / "lib"
queue = [pkg / "chromaprint3d_server"] + sorted(lib_dir.glob("libonnxruntime*.dylib*"))
seen = set()

search_dirs = []
for key in ("BREW_PREFIX", "OPENCV_PREFIX", "LIBOMP_PREFIX", "JPEG_PREFIX"):
    value = os.environ.get(key)
    if value:
        search_dirs.append(Path(value) / "lib")
search_dirs.append(Path("build/_deps/onnxruntime-src/lib").resolve())
search_dirs.extend([Path("/usr/local/lib"), Path("/opt/homebrew/lib")])
search_dirs = [p for p in search_dirs if p.exists()]


def resolve_dep(dep: str, owner: Path):
    if dep.startswith("@loader_path/"):
        candidate = (owner.parent / dep[len("@loader_path/"):]).resolve()
        return candidate if candidate.exists() else None
    if dep.startswith("@executable_path/"):
        candidate = (pkg / dep[len("@executable_path/"):]).resolve()
        return candidate if candidate.exists() else None
    if dep.startswith("@rpath/"):
        base = os.path.basename(dep)
        for directory in [owner.parent, *search_dirs]:
            candidate = (Path(directory) / base).resolve()
            if candidate.exists():
                return candidate
        return None
    candidate = Path(dep)
    return candidate.resolve() if candidate.exists() else None


while queue:
    owner = queue.pop(0)
    if not owner.exists():
        continue
    owner_key = str(owner.resolve())
    if owner_key in seen:
        continue
    seen.add(owner_key)

    output = subprocess.check_output(["otool", "-L", str(owner)], text=True)
    for raw in output.splitlines()[1:]:
        dep = raw.strip().split(" (", 1)[0]
        if not dep or dep.startswith("/System/") or dep.startswith("/usr/lib/"):
            continue
        source = resolve_dep(dep, owner)
        if source is None:
            continue
        dest = lib_dir / source.name
        if not dest.exists():
            shutil.copy2(source, dest)
        queue.append(dest)

print("Bundled macOS runtime libraries:")
for path in sorted(lib_dir.iterdir()):
    if path.is_file():
        print(" -", path.name)
PY

for file in pkg/chromaprint3d_server pkg/lib/*.dylib*; do
  [ -f "$file" ] || continue
  while IFS= read -r dep; do
    base="$(basename "$dep")"
    if [ -f "pkg/lib/$base" ]; then
      install_name_tool -change "$dep" "@rpath/$base" "$file" || true
    fi
  done < <(otool -L "$file" | awk 'NR>1 {print $1}')
done

install_name_tool -add_rpath "@executable_path/lib" pkg/chromaprint3d_server || true
for lib in pkg/lib/*.dylib*; do
  [ -f "$lib" ] || continue
  base="$(basename "$lib")"
  install_name_tool -id "@rpath/$base" "$lib" || true
  install_name_tool -add_rpath "@loader_path" "$lib" || true
done

cat > pkg/run_chromaprint3d_server.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DYLD_LIBRARY_PATH="${SCRIPT_DIR}/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
exec "${SCRIPT_DIR}/chromaprint3d_server" "$@"
EOF
chmod +x pkg/run_chromaprint3d_server.sh

echo "Packaged macOS files:"
ls -lh pkg/

#!/usr/bin/env bash
set -euo pipefail

mkdir -p pkg/lib
cp build/bin/chromaprint3d_server pkg/
find build/_deps/onnxruntime-src/lib -name "libonnxruntime*.so*" \
  -exec cp {} pkg/lib/ \; 2>/dev/null || true

python3 - <<'PY'
import os
import re
import shutil
import subprocess
from pathlib import Path

pkg = Path("pkg")
lib_dir = pkg / "lib"
targets = [pkg / "chromaprint3d_server"] + sorted(lib_dir.glob("libonnxruntime*.so*"))
dep_re = re.compile(r"=>\s+(\S+)\s+\(")
skip_names = {
    "linux-vdso.so.1",
    "libc.so.6",
    "libm.so.6",
    "libpthread.so.0",
    "librt.so.1",
    "libdl.so.2",
    "libstdc++.so.6",
    "libgcc_s.so.1",
    "ld-linux-x86-64.so.2",
    "ld-linux-aarch64.so.1",
}

copied = set()
for target in targets:
    if not target.exists():
        continue
    output = subprocess.check_output(["ldd", str(target)], text=True, stderr=subprocess.STDOUT)
    for raw in output.splitlines():
        line = raw.strip()
        if not line or "not found" in line:
            continue
        match = dep_re.search(line)
        dep = match.group(1) if match else (line.split()[0] if line.startswith("/") else "")
        if not dep or not dep.startswith("/"):
            continue
        name = os.path.basename(dep)
        if name in skip_names or name in copied:
            continue
        shutil.copy2(dep, lib_dir / name)
        copied.add(name)

print("Bundled Linux runtime libraries:")
for path in sorted(lib_dir.iterdir()):
    if path.is_file():
        print(" -", path.name)
PY

patchelf --set-rpath '$ORIGIN/lib' pkg/chromaprint3d_server
for so in pkg/lib/*.so*; do
  [ -f "$so" ] || continue
  patchelf --set-rpath '$ORIGIN' "$so" || true
done

cat > pkg/run_chromaprint3d_server.sh <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${SCRIPT_DIR}/chromaprint3d_server" "$@"
EOF
chmod +x pkg/run_chromaprint3d_server.sh

echo "Packaged Linux files:"
ls -lh pkg/

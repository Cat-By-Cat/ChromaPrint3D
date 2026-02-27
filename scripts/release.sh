#!/usr/bin/env bash
#
# Usage: ./scripts/release.sh <new_version>
#   e.g. ./scripts/release.sh 1.2.0
#
# Steps:
#   1. Validate version format
#   2. Check working tree is clean
#   3. Full rebuild & verify compilation
#   4. Update version in CMakeLists.txt and web/frontend/package.json
#   5. Commit, tag, and push to origin

set -euo pipefail

# ---------- Helpers ----------

die() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "==> $*"; }

# ---------- Args ----------

VERSION="${1:-}"
[[ -z "$VERSION" ]] && die "Usage: $0 <version>  (e.g. 1.2.0)"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "Version must be MAJOR.MINOR.PATCH (got: $VERSION)"

TAG="v${VERSION}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# ---------- Pre-checks ----------

if ! git diff --quiet || ! git diff --cached --quiet; then
    die "Working tree is dirty. Commit or stash changes first."
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
    die "Tag $TAG already exists."
fi

BRANCH="$(git symbolic-ref --short HEAD)"
info "Current branch: $BRANCH"

# ---------- Version continuity check ----------

PREV_TAG="$(git tag --sort=-v:refname -l 'v[0-9]*' | head -1)"
if [[ -n "$PREV_TAG" ]]; then
    PREV="${PREV_TAG#v}"
    IFS='.' read -r PM Pm Pp <<< "$PREV"
    IFS='.' read -r NM Nm Np <<< "$VERSION"

    is_successor=false
    if   (( NM == PM + 1 && Nm == 0 && Np == 0 )); then is_successor=true  # major bump
    elif (( NM == PM && Nm == Pm + 1 && Np == 0 )); then is_successor=true  # minor bump
    elif (( NM == PM && Nm == Pm && Np == Pp + 1 )); then is_successor=true # patch bump
    fi

    if [[ "$is_successor" == false ]]; then
        echo "WARNING: $PREV -> $VERSION is not a sequential bump."
        read -rp "Continue anyway? [y/N] " ans
        [[ "$ans" =~ ^[Yy]$ ]] || die "Aborted."
    fi

    info "Version: $PREV -> $VERSION"
else
    info "No previous tag found, this will be the first release."
fi

# ---------- 1. Full rebuild ----------

info "Clean rebuild to verify compilation..."
BUILD_DIR="$ROOT/build"
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCHROMAPRINT3D_BUILD_TESTS=OFF "$ROOT"
cmake --build "$BUILD_DIR" --parallel "$(nproc)" --clean-first

info "Building web frontend..."
(cd "$ROOT/web/frontend" && npm run build)

info "Build passed."

# ---------- 2. Update versions ----------

info "Updating version to $VERSION..."

# CMakeLists.txt
sed -i "s/project(ChromaPrint3D VERSION [0-9]\+\.[0-9]\+\.[0-9]\+/project(ChromaPrint3D VERSION ${VERSION}/" \
    "$ROOT/CMakeLists.txt"

# web/frontend/package.json
sed -i "s/\"version\": \"[0-9]\+\.[0-9]\+\.[0-9]\+\"/\"version\": \"${VERSION}\"/" \
    "$ROOT/web/frontend/package.json"

# Verify changes applied
grep -q "VERSION ${VERSION}" "$ROOT/CMakeLists.txt" || die "Failed to update CMakeLists.txt"
grep -q "\"version\": \"${VERSION}\"" "$ROOT/web/frontend/package.json" || die "Failed to update package.json"

# ---------- 3. Commit, tag, push ----------

info "Committing version bump..."
git add CMakeLists.txt web/frontend/package.json
git commit -m "release: v${VERSION}"

info "Creating tag $TAG..."
git tag -a "$TAG" -m "Release ${TAG}"

info "Pushing to origin..."
git push origin "$BRANCH"
git push origin "$TAG"

info "Done! Tag $TAG pushed. GitHub Actions will create the release."

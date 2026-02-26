#!/usr/bin/env bash
#
# Deploy split architecture:
#   1) Build web frontend locally and upload to cloud host
#   2) Restart backend stack on home host (down -> pull -> up -d)
#
# Usage:
#   ./scripts/deploy_split.sh [config-file]
#
# Config bootstrap:
#   cp scripts/deploy_split.example.env scripts/deploy_split.env
#   # edit scripts/deploy_split.env
#   ./scripts/deploy_split.sh
#
set -euo pipefail

die() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "==> $*"; }

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEFAULT_CONFIG="$ROOT_DIR/scripts/deploy_split.env"
CONFIG_PATH="${1:-$DEFAULT_CONFIG}"

[[ -f "$CONFIG_PATH" ]] || die "Config file not found: $CONFIG_PATH (copy from scripts/deploy_split.example.env)"

# shellcheck disable=SC1090
source "$CONFIG_PATH"

required_vars=(
    CLOUD_HOST
    CLOUD_WEB_ROOT
    HOME_HOST
    HOME_DEPLOY_DIR
)
for var_name in "${required_vars[@]}"; do
    [[ -n "${!var_name:-}" ]] || die "Missing required config: $var_name"
done

CLOUD_SSH_PORT="${CLOUD_SSH_PORT:-22}"
HOME_SSH_PORT="${HOME_SSH_PORT:-22}"
CLOUD_UPLOAD_TAR="${CLOUD_UPLOAD_TAR:-/tmp/chromaprint3d-web.tar.gz}"
CLOUD_USE_SUDO="${CLOUD_USE_SUDO:-1}"
HOME_USE_SUDO="${HOME_USE_SUDO:-0}"
CLOUD_RELOAD_NGINX="${CLOUD_RELOAD_NGINX:-1}"
WEB_INSTALL_DEPS="${WEB_INSTALL_DEPS:-0}"

for cmd in ssh scp tar npm; do
    command -v "$cmd" >/dev/null 2>&1 || die "Required command not found: $cmd"
done

cloud_ssh_opts=(-p "$CLOUD_SSH_PORT")
home_ssh_opts=(-p "$HOME_SSH_PORT")

# Remote sudo may require an interactive TTY.
if [[ "$CLOUD_USE_SUDO" == "1" ]]; then
    cloud_ssh_opts+=(-tt)
fi
if [[ "$HOME_USE_SUDO" == "1" ]]; then
    home_ssh_opts+=(-tt)
fi

if [[ "$WEB_INSTALL_DEPS" == "1" ]]; then
    info "Installing web dependencies with npm ci..."
    (cd "$ROOT_DIR/web" && npm ci)
fi

info "Building web frontend..."
if [[ -n "${VITE_API_BASE:-}" ]]; then
    info "Using VITE_API_BASE=$VITE_API_BASE"
    (cd "$ROOT_DIR/web" && VITE_API_BASE="$VITE_API_BASE" npm run build)
else
    (cd "$ROOT_DIR/web" && npm run build)
fi

WEB_DIST_DIR="$ROOT_DIR/web/dist"
[[ -d "$WEB_DIST_DIR" ]] || die "Build output not found: $WEB_DIST_DIR"

TMP_TAR="$(mktemp /tmp/chromaprint3d-web.XXXXXX.tar.gz)"
trap 'rm -f "$TMP_TAR"' EXIT
tar -C "$WEB_DIST_DIR" -czf "$TMP_TAR" .
info "Packaged web assets into $TMP_TAR"

info "Uploading web bundle to cloud host..."
scp -P "$CLOUD_SSH_PORT" "$TMP_TAR" "$CLOUD_HOST:$CLOUD_UPLOAD_TAR"

info "Applying web bundle on cloud host..."
CLOUD_SCRIPT="$(mktemp /tmp/chromaprint3d-cloud-deploy.XXXXXX.sh)"
cat > "$CLOUD_SCRIPT" <<'CLOUD_SCRIPT_BODY'
#!/usr/bin/env bash
set -euo pipefail

cloud_web_root="$1"
cloud_upload_tar="$2"
cloud_use_sudo="$3"
cloud_reload_nginx="$4"

if [[ -z "$cloud_web_root" || "$cloud_web_root" == "/" ]]; then
    echo "ERROR: Refusing to deploy to unsafe CLOUD_WEB_ROOT: '$cloud_web_root'" >&2
    exit 1
fi

run_cmd() {
    if [[ "$cloud_use_sudo" == "1" ]]; then
        sudo "$@"
    else
        "$@"
    fi
}

if [[ "$cloud_use_sudo" == "1" ]]; then
    sudo -v
fi

run_cmd mkdir -p "$cloud_web_root"
run_cmd find "$cloud_web_root" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
run_cmd tar -xzf "$cloud_upload_tar" -C "$cloud_web_root"
rm -f "$cloud_upload_tar"

if [[ "$cloud_reload_nginx" == "1" ]]; then
    if [[ "$cloud_use_sudo" == "1" ]]; then
        sudo nginx -t
        sudo systemctl reload nginx
    else
        nginx -t
        systemctl reload nginx
    fi
fi
CLOUD_SCRIPT_BODY

CLOUD_REMOTE_SCRIPT="/tmp/chromaprint3d-cloud-deploy.$$.sh"
scp -P "$CLOUD_SSH_PORT" "$CLOUD_SCRIPT" "$CLOUD_HOST:$CLOUD_REMOTE_SCRIPT"
rm -f "$CLOUD_SCRIPT"
ssh "${cloud_ssh_opts[@]}" "$CLOUD_HOST" \
    "bash '$CLOUD_REMOTE_SCRIPT' '$CLOUD_WEB_ROOT' '$CLOUD_UPLOAD_TAR' '$CLOUD_USE_SUDO' '$CLOUD_RELOAD_NGINX'; rm -f '$CLOUD_REMOTE_SCRIPT'"

info "Restarting backend stack on home host..."
HOME_SCRIPT="$(mktemp /tmp/chromaprint3d-home-deploy.XXXXXX.sh)"
cat > "$HOME_SCRIPT" <<'HOME_SCRIPT_BODY'
#!/usr/bin/env bash
set -euo pipefail

home_deploy_dir="$1"
home_use_sudo="$2"

if [[ -z "$home_deploy_dir" || "$home_deploy_dir" == "/" ]]; then
    echo "ERROR: Refusing to run docker compose in unsafe HOME_DEPLOY_DIR: '$home_deploy_dir'" >&2
    exit 1
fi

if docker compose version >/dev/null 2>&1; then
    compose_cmd=(docker compose)
elif command -v docker-compose >/dev/null 2>&1; then
    compose_cmd=(docker-compose)
else
    echo "ERROR: docker compose/docker-compose is not available." >&2
    exit 1
fi

run_compose() {
    if [[ "$home_use_sudo" == "1" ]]; then
        sudo "${compose_cmd[@]}" "$@"
    else
        "${compose_cmd[@]}" "$@"
    fi
}

if [[ "$home_use_sudo" == "1" ]]; then
    sudo -v
fi

cd "$home_deploy_dir"
run_compose down
run_compose pull
run_compose up -d
run_compose ps
HOME_SCRIPT_BODY

HOME_REMOTE_SCRIPT="/tmp/chromaprint3d-home-deploy.$$.sh"
scp -P "$HOME_SSH_PORT" "$HOME_SCRIPT" "$HOME_HOST:$HOME_REMOTE_SCRIPT"
rm -f "$HOME_SCRIPT"
ssh "${home_ssh_opts[@]}" "$HOME_HOST" \
    "bash '$HOME_REMOTE_SCRIPT' '$HOME_DEPLOY_DIR' '$HOME_USE_SUDO'; rm -f '$HOME_REMOTE_SCRIPT'"

info "Split deployment completed successfully."

#!/usr/bin/env bash
set -euo pipefail

# OhMyClawd daemon installer
# Usage: curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh | sudo bash  # Linux
#        curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh | bash       # macOS
#
# Environment variables (optional):
#   OHMYCLAWD_USER    — user who runs Claude Code (defaults to SUDO_USER on Linux/current user on macOS)
#   OHMYCLAWD_LISTEN  — bind address (default: 127.0.0.1:8787 on Linux, :8787 on macOS)
#   OHMYCLAWD_TOKEN   — bearer token for /usage and /metrics (optional)

REPO="opariffazman/ohmyclawd"
SERVICE_NAME="ohmyclawd-daemon"
OS="$(uname -s)"
ARCH="$(uname -m)"

xml_escape() {
  printf '%s' "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g"
}

daemon_host() {
  local host
  host="$(hostname)"
  if [[ "${host}" == *.local ]]; then
    printf '%s' "${host}"
  else
    printf '%s.local' "${host}"
  fi
}

case "${OS}" in
  Linux)
    BINARY="ohmyclawd-daemon-linux-amd64"
    INSTALL_DIR="/usr/local/bin"
    SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
    DEFAULT_LISTEN="127.0.0.1:8787"

    if [[ "${EUID}" -ne 0 ]]; then
      echo "error: must run as root (sudo) on Linux" >&2
      exit 1
    fi

    TARGET_USER="${OHMYCLAWD_USER:-${SUDO_USER:-}}"
    if [[ -z "${TARGET_USER}" ]]; then
      echo "error: set OHMYCLAWD_USER=<user who runs Claude Code> or invoke via sudo" >&2
      exit 1
    fi
    if ! id -u "${TARGET_USER}" >/dev/null 2>&1; then
      echo "error: user '${TARGET_USER}' does not exist" >&2
      exit 1
    fi
    ;;
  Darwin)
    case "${ARCH}" in
      arm64) BINARY="ohmyclawd-daemon-darwin-arm64" ;;
      x86_64) BINARY="ohmyclawd-daemon-darwin-amd64" ;;
      *)
        echo "error: unsupported macOS architecture '${ARCH}'" >&2
        exit 1
        ;;
    esac

    TARGET_USER="${OHMYCLAWD_USER:-${SUDO_USER:-${USER:-}}}"
    if [[ -z "${TARGET_USER}" ]]; then
      echo "error: could not determine the macOS user" >&2
      exit 1
    fi
    if [[ "${EUID}" -ne 0 && "${TARGET_USER}" != "$(id -un)" ]]; then
      echo "error: macOS installs must target the current user unless run with sudo" >&2
      exit 1
    fi
    if ! id -u "${TARGET_USER}" >/dev/null 2>&1; then
      echo "error: user '${TARGET_USER}' does not exist" >&2
      exit 1
    fi
    if [[ "${EUID}" -eq 0 && -z "${SUDO_USER:-}" && -z "${OHMYCLAWD_USER:-}" ]]; then
      echo "error: set OHMYCLAWD_USER=<user who runs Claude Code> when running as root" >&2
      exit 1
    fi

    TARGET_HOME="$(dscl . -read "/Users/${TARGET_USER}" NFSHomeDirectory | awk '{print $2}' || true)"
    if [[ -z "${TARGET_HOME}" ]]; then
      echo "error: could not determine home directory for '${TARGET_USER}'" >&2
      exit 1
    fi
    TARGET_UID="$(id -u "${TARGET_USER}")"
    INSTALL_DIR="${TARGET_HOME}/.local/bin"
    PLIST_DIR="${TARGET_HOME}/Library/LaunchAgents"
    PLIST_FILE="${PLIST_DIR}/local.${SERVICE_NAME}.plist"
    LOG_DIR="${TARGET_HOME}/Library/Logs"
    DEFAULT_LISTEN=":8787"
    ;;
  *)
    echo "error: unsupported OS '${OS}'" >&2
    exit 1
    ;;
esac

# --- Preserve existing config on upgrade ---
EXISTING_LISTEN=""
EXISTING_TOKEN=""
EXISTING_PROBE=""
if [[ "${OS}" == "Linux" && -f "${SERVICE_FILE}" ]]; then
  EXISTING_LISTEN=$(grep -oP '(?<=Environment=OHMYCLAWD_LISTEN=).+' "${SERVICE_FILE}" 2>/dev/null || true)
  EXISTING_TOKEN=$(grep -oP '(?<=Environment=OHMYCLAWD_TOKEN=).+' "${SERVICE_FILE}" 2>/dev/null || true)
  EXISTING_PROBE=$(grep -oP '(?<=Environment=OHMYCLAWD_PROBE_INTERVAL=).+' "${SERVICE_FILE}" 2>/dev/null || true)
elif [[ "${OS}" == "Darwin" && -f "${PLIST_FILE}" ]]; then
  EXISTING_LISTEN=$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:OHMYCLAWD_LISTEN' "${PLIST_FILE}" 2>/dev/null || true)
  EXISTING_TOKEN=$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:OHMYCLAWD_TOKEN' "${PLIST_FILE}" 2>/dev/null || true)
  EXISTING_PROBE=$(/usr/libexec/PlistBuddy -c 'Print :EnvironmentVariables:OHMYCLAWD_PROBE_INTERVAL' "${PLIST_FILE}" 2>/dev/null || true)
fi

LISTEN="${OHMYCLAWD_LISTEN:-${EXISTING_LISTEN:-${DEFAULT_LISTEN}}}"
TOKEN="${OHMYCLAWD_TOKEN:-${EXISTING_TOKEN:-}}"
PROBE="${OHMYCLAWD_PROBE_INTERVAL:-${EXISTING_PROBE:-60s}}"

echo "==> fetching latest release..."
DOWNLOAD_URL=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
  | grep "browser_download_url.*${BINARY}" \
  | cut -d '"' -f 4)

if [[ -z "${DOWNLOAD_URL}" ]]; then
  echo "error: could not find ${BINARY} in latest release" >&2
  exit 1
fi

# --- Stop service before replacing binary (avoids "Text file busy") ---
if [[ "${OS}" == "Linux" ]] && systemctl is-active --quiet "${SERVICE_NAME}" 2>/dev/null; then
  echo "==> stopping ${SERVICE_NAME}..."
  systemctl stop "${SERVICE_NAME}"
elif [[ "${OS}" == "Darwin" ]]; then
  launchctl bootout "gui/${TARGET_UID}/local.${SERVICE_NAME}" 2>/dev/null || true
fi

echo "==> downloading ${DOWNLOAD_URL}..."
curl -fsSL -o "/tmp/${BINARY}" "${DOWNLOAD_URL}"
mkdir -p "${INSTALL_DIR}"
install -m 0755 "/tmp/${BINARY}" "${INSTALL_DIR}/ohmyclawd-daemon"
rm -f "/tmp/${BINARY}"
if [[ "${OS}" == "Darwin" && "${EUID}" -eq 0 ]]; then
  chown "${TARGET_USER}" "${INSTALL_DIR}" "${INSTALL_DIR}/ohmyclawd-daemon"
fi

if [[ "${OS}" == "Darwin" ]]; then
  mkdir -p "${PLIST_DIR}" "${LOG_DIR}"
  if [[ "${EUID}" -eq 0 ]]; then
    chown "${TARGET_USER}" "${PLIST_DIR}" "${LOG_DIR}"
  fi

  TOKEN_PLIST=""
  if [[ -n "${TOKEN}" ]]; then
    TOKEN_PLIST="    <key>OHMYCLAWD_TOKEN</key>
    <string>$(xml_escape "${TOKEN}")</string>"
  fi

  echo "==> installing launchd agent for user '${TARGET_USER}'..."
  cat > "${PLIST_FILE}" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>local.${SERVICE_NAME}</string>
  <key>ProgramArguments</key>
  <array>
    <string>${INSTALL_DIR}/ohmyclawd-daemon</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>OHMYCLAWD_LISTEN</key>
    <string>$(xml_escape "${LISTEN}")</string>
    <key>OHMYCLAWD_PROBE_INTERVAL</key>
    <string>$(xml_escape "${PROBE}")</string>
${TOKEN_PLIST}
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>${LOG_DIR}/${SERVICE_NAME}.log</string>
  <key>StandardErrorPath</key>
  <string>${LOG_DIR}/${SERVICE_NAME}.log</string>
</dict>
</plist>
EOF
  if [[ "${EUID}" -eq 0 ]]; then
    chown "${TARGET_USER}" "${PLIST_FILE}"
  fi
  plutil -lint "${PLIST_FILE}"
  launchctl bootstrap "gui/${TARGET_UID}" "${PLIST_FILE}"
  launchctl kickstart -k "gui/${TARGET_UID}/local.${SERVICE_NAME}"

  echo "==> done! ohmyclawd-daemon is running"
  echo ""
  echo "config:"
  echo "  listen: ${LISTEN}"
  echo "  CYD URL: http://$(daemon_host):${LISTEN##*:}"
  if [[ -n "${TOKEN}" ]]; then
    echo "  token:  set (${#TOKEN} chars)"
    echo "  usage:  curl -H 'Authorization: Bearer <token>' http://$(daemon_host):${LISTEN##*:}/usage"
  else
    echo "  token:  none (open access)"
    echo "  usage:  curl http://$(daemon_host):${LISTEN##*:}/usage"
  fi
  echo "  logs:    ${LOG_DIR}/${SERVICE_NAME}.log"
  exit 0
fi

# --- Build token line (only if set) ---
TOKEN_LINE=""
if [[ -n "${TOKEN}" ]]; then
  TOKEN_LINE="Environment=OHMYCLAWD_TOKEN=${TOKEN}"
fi

echo "==> installing systemd service for user '${TARGET_USER}'..."
cat > "${SERVICE_FILE}" << EOF
[Unit]
Description=ohmyclawd daemon — probes Anthropic for Claude Code utilization
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
ExecStart=${INSTALL_DIR}/ohmyclawd-daemon
Restart=always
RestartSec=5
User=${TARGET_USER}
Group=${TARGET_USER}
# Bind address: 127.0.0.1:8787 (localhost) or :8787 (LAN)
Environment=OHMYCLAWD_LISTEN=${LISTEN}
Environment=OHMYCLAWD_PROBE_INTERVAL=${PROBE}
${TOKEN_LINE}
NoNewPrivileges=true
PrivateTmp=false
ProtectSystem=strict
ProtectHome=read-only

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}.service"
systemctl restart "${SERVICE_NAME}.service"

echo "==> done! ohmyclawd-daemon is running"
systemctl --no-pager status "${SERVICE_NAME}.service" | head -5
echo ""
echo "config:"
echo "  listen: ${LISTEN}"
if [[ -n "${TOKEN}" ]]; then
  echo "  token:  set (${#TOKEN} chars)"
  echo "  usage:  curl -H 'Authorization: Bearer <token>' http://$(daemon_host):${LISTEN##*:}/usage"
else
  echo "  token:  none (open access)"
  echo "  usage:  curl http://$(daemon_host):${LISTEN##*:}/usage"
fi

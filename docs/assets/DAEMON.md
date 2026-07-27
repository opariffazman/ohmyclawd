# ohmyclawd daemon

Probes Anthropic `/v1/messages` every 60s with 1-Haiku-token call. Parses `anthropic-ratelimit-unified-*` headers into compact JSON. ESP32 polls over HTTP.

## Install

```bash
# remote install (downloads latest release binary)
curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh | sudo bash

# with auth token
curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh \
  | OHMYCLAWD_TOKEN=yourpassphrase sudo -E bash

# local build install
sudo OHMYCLAWD_USER=youruser ./install.sh
```

Builds static binary → `/usr/local/bin/ohmyclawd-daemon`. Installs systemd unit. Runs as target user to read `~/.claude/.credentials.json`; on macOS, current Claude Code credentials are read from the login Keychain automatically.

## Config

Env vars on systemd unit. Edit `/etc/systemd/system/ohmyclawd-daemon.service`, then `systemctl daemon-reload && systemctl restart ohmyclawd-daemon`.

| Var | Default | What |
|-----|---------|------|
| `OHMYCLAWD_LISTEN` | `127.0.0.1:8787` | Bind address. `:8787` for LAN |
| `OHMYCLAWD_TOKEN` | *(empty)* | Bearer token for `/usage` and `/metrics` |
| `OHMYCLAWD_PROBE_INTERVAL` | `60s` | Poll frequency |
| `OHMYCLAWD_CREDS_PATH` | legacy file path, otherwise macOS Keychain fallback | OAuth creds path |
| `OHMYCLAWD_ANTHROPIC_URL` | `https://api.anthropic.com/v1/messages` | API endpoint |

### Access modes

| Scenario | LISTEN | TOKEN |
|----------|--------|-------|
| Local only (default) | `127.0.0.1:8787` | unset |
| LAN no auth | `:8787` | unset |
| LAN + auth | `:8787` | `<passphrase>` |
| CF Tunnel (remote) | `127.0.0.1:8787` | `<passphrase>` |

## Endpoints

| Path | Auth | Response |
|------|------|----------|
| `GET /usage` | token (if set) | JSON `{s, sr, w, wr, st, ok, ts, cs, cw}`. Supports `If-None-Match` |
| `GET /healthz` | none | `ok` plain text |
| `GET /metrics` | token (if set) | Prometheus exposition |

## Fake mode

```bash
ohmyclawd-daemon --fake
```

Scripted utilization curve. No Anthropic calls. Test firmware in isolation.

## Tests

```bash
go test -race -count=1 ./...
```

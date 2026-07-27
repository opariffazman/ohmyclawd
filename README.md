```
 ██████  ██   ██     ███    ███ ██    ██
██    ██ ██   ██     ████  ████  ██  ██
██    ██ ███████     ██ ████ ██   ████
██    ██ ██   ██     ██  ██  ██    ██
 ██████  ██   ██     ██      ██    ██

 ██████ ██       █████  ██     ██ ██████
██      ██      ██   ██ ██     ██ ██   ██
██      ██      ███████ ██  █  ██ ██   ██
██      ██      ██   ██ ██ ███ ██ ██   ██
 ██████ ███████ ██   ██  ███ ███  ██████
```

Claude Code usage monitor on ESP32-2432S028R (CYD 2.8") with pixel art animations.

<img src="docs/assets/mode_sprite.gif" width="250"> | <img src="docs/assets/mode_clock.gif" width="250"> | <img src="docs/assets/mode_settings.gif" width="250">
:---:|:---:|:---:

Real-time session + weekly usage. Animated pixel sprites. Digital clock.

## Features

- **Usage bars** — session + weekly utilization at a glance
- **13 pixel sprites** — changes based on activity state
- **Tmux detection** — knows when Claude waiting for input
- **OTA updates** — checks GitHub releases on boot
- **Captive portal config** — no code changes for WiFi/daemon setup
- **Pixel clock** — retro digital clock + second-progress bar
- **On-device settings** — brightness, quiet hours, auto-cycle, sprite mode, orientation, factory reset
- **Page nav** — indicator dots + `<` `>` tap buttons
- **Offline indicator** — pixel `X` + colour-drain when unreachable

## Hardware

| | |
|---|---|
| Board | ESP32-2432S028R (CYD 2.8") |
| Display | 2.8" ILI9341 320x240 TFT |
| Touch | XPT2046 resistive |
| Connectivity | WiFi 2.4 GHz |

## Quick Start

### 1. Install daemon

Runs on your machine. Polls Anthropic rate-limit headers, serves usage JSON.

```bash
# basic
curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh | sudo bash

# with auth (recommended for remote/tunnel access)
curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh \
  | OHMYCLAWD_TOKEN=yourpassphrase sudo -E bash
```

Other platforms — [Releases](https://github.com/opariffazman/ohmyclawd/releases):

| Platform | Binary |
|----------|--------|
| Linux x64 | `ohmyclawd-daemon-linux-amd64` |
| macOS x64 | `ohmyclawd-daemon-darwin-amd64` |
| macOS ARM | `ohmyclawd-daemon-darwin-arm64` |
| Windows x64 | `ohmyclawd-daemon-windows-amd64.exe` |

```bash
# Linux/macOS
chmod +x ohmyclawd-daemon-*
OHMYCLAWD_TOKEN=yourpassphrase ./ohmyclawd-daemon-linux-amd64

# Windows
set OHMYCLAWD_TOKEN=yourpassphrase
ohmyclawd-daemon-windows-amd64.exe
```

### 2. Flash firmware

**Browser (easiest):** [Web Flasher](https://opariffazman.github.io/ohmyclawd/) — USB connect, click install.

**PlatformIO:**

```bash
git clone https://github.com/opariffazman/ohmyclawd.git && cd ohmyclawd
pio run -e cyd -t upload
```

**esptool:** download `ohmyclawd-firmware.bin` from [Releases](https://github.com/opariffazman/ohmyclawd/releases):

```bash
esptool.py write_flash 0x10000 ohmyclawd-firmware.bin
```

### 3. Configure CYD

First boot creates WiFi AP:

1. Connect to **`OhMyClawd`** on phone
2. Captive portal opens (or `192.168.4.1`)
3. Enter **WiFi SSID** + **password**
4. Set **Daemon URL** (default: `http://ohmyclawd.local:8787`)
5. Set **Daemon Token** (must match `OHMYCLAWD_TOKEN` on daemon)
6. Set **Timezone** ([POSIX TZ format](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html))
7. Save — reboots and connects

Settings persist. Hold 5s to reset, or use RESET row in settings (3s hold).

### 4. Done

Tap = cycle sprites. `<` `>` = switch modes.

## Sprite States

Requires Claude Code in tmux:

| State | Sprite | Trigger |
|---|---|---|
| Needs input | expression-surprise, expression-wink | Session idle >30s |
| Rate limited | expression-sleep, idle-breathe | Usage ≥ 80% |
| Heavy | work-think, idle-look-around | Usage 50–79% |
| Moderate | work-coding, dance-djmix | Usage 25–49% |
| Light | dance-bounce, dance-sway, bounce-dj, sway-dj, idle-blink | Usage < 25% |

## Settings

Reach via swipe or nav buttons (auto-cycle skips settings).

`*` rows = **hold-drag sliders**: hold 0.5s → orange bar → drag. Tap **SAVE** to persist.

| Row | Input | What |
|---|---|---|
| BRIGHTNESS * | hold-drag | 10–100% PWM, live preview |
| QUIET START * | hold-drag | Hour 0–23 |
| QUIET END * | hold-drag | Hour 0–23, cross-midnight OK (22→07) |
| QUIET MODE | tap | OFF / DIM / SLEEP |
| AUTO-CYCLE * | hold-drag | 5–250s interval, 0 = OFF |
| SPRITE MODE | tap | DYNAMIC / FREE |
| ORIENTATION | tap | NORMAL / FLIPPED |
| INVERT COLORS | tap | ON / OFF |
| RESET | hold 3s | Clears WiFi + settings, reboots |
| SAVE | tap | Persist to NVS |

SLEEP mode: screen dark. Tap = wake 10s.

Offline: bars + sprite drain grey, pulsing red `X` top-right. Auto-recovers.

## Updates

**Firmware:** OTA on boot — checks GitHub, prompts to update.

**Daemon:** re-run install script:

```bash
curl -fsSL https://raw.githubusercontent.com/opariffazman/ohmyclawd/master/install.sh | sudo bash
```

## Further Docs

- [Daemon config, auth & access modes](docs/assets/DAEMON.md)
- [Native screen recording](docs/assets/NATIVE_RECORDING.md)

## How It Works

Daemon reads `~/.claude/.credentials.json` (auto-created by Claude Code auth). Makes lightweight API requests, reads rate-limit response headers for session/weekly usage %. No messages sent or read — headers only.

## Credits

- Pixel animations from [claudepix](https://claudepix.vercel.app/)

## Disclaimer

Unofficial. Not affiliated with Anthropic. Relies on undocumented rate-limit headers — may change without notice. Read-only access to local credentials file. Tokens never transmitted or exposed over network. Use at own risk.

## License

[MIT](LICENSE)

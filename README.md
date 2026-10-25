<div align="center">

# ESP32 WiFi Repeater

**Share your WiFi on purpose. See it. Limit it.**

An open-source ESP32 repeater that extends your home WiFi, counts the data each device uses, enforces a daily quota per device, and gives you a live dashboard in your browser and a bot in Telegram.

[![Build](https://github.com/CodeVortax/esp32-wifi-repeater-manage/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/CodeVortax/esp32-wifi-repeater-manage/actions/workflows/build-firmware.yml)
[![Version](https://img.shields.io/badge/version-0.2.0--beta-4285F4?style=flat-square)](CHANGELOG.md)
[![License: AGPL-3.0-only](https://img.shields.io/badge/license-AGPL--3.0--only-34A853?style=flat-square)](LICENSE)
[![Chip](https://img.shields.io/badge/chip-ESP32-E7352C?style=flat-square&logo=espressif&logoColor=white)](#install)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-Arduino-F5822A?style=flat-square&logo=platformio&logoColor=white)](#tech-stack)
[![Cloud](https://img.shields.io/badge/cloud-none-2e8b57?style=flat-square)](#privacy)

**English** · [العربية](README.ar.md)

[Features](#features) · [How it works](#how-it-works) · [Install](#install) · [Dashboard](#the-dashboard) · [Telegram](#telegram-commands) · [Documentation](#documentation) · [Contributing](CONTRIBUTING.md) · [Changelog](CHANGELOG.md)

</div>

---

## Screenshots

<p align="center">
  <img src="docs/images/dashboard-en.png" width="420" alt="Dashboard: devices, usage bars, live domains, ping and settings">
  <img src="docs/images/dashboard-ar.png" width="150" alt="The same dashboard in Arabic, right to left">
</p>

## What is it?

A repeater built on one idea: **whoever uses your internet should be visible, and capped.**

The ESP32 joins your router as a client and opens its own network next to it. Phones and laptops connect to the ESP32, and their traffic is forwarded to your router (NAT). Because every byte passes through the ESP32, it can count usage per device, cut a device off when it hits its daily quota, show you the domains being looked up, and report to you over Telegram.

Everything runs on the chip. There is no cloud, no account and no app to install.

## Features

### Repeating

| | |
|---|---|
| **AP + STA with NAT** | The ESP32 connects to your router and shares it on its own network. NAT turns on as soon as the router gives an address, and again after every reconnect. |
| **Background connect** | Startup never waits for the router. The dashboard is reachable immediately. |
| **DNS through the ESP32** | DHCP hands out the ESP32 as the DNS server, so every lookup is seen and then forwarded to your router's DNS. |

### Quota and control

| | |
|---|---|
| **Per-device counting** | Counted at the network-stack level, in **both directions**, per device MAC. See [Quota](docs/architecture/quota.md). |
| **Daily quota** | Default 1 GB per device per day. Change it per device, or set `0` for unlimited. |
| **Block / whitelist** | A blocked device stays connected but gets no traffic. Whitelisted devices are never limited. |
| **Local midnight reset** | Time-zone and daylight-saving aware (Egypt by default). A reboot across midnight still resets correctly. |
| **Remembers devices** | Limits, whitelist, blocks and counters are kept in flash, written every minute, and only when something changed. |

### Monitoring

- **Router-style dashboard** with a login screen: Overview, Users, Apps, Speed, Settings, System. Arabic / English, full right-to-left.
- **Named users** ("User 1, 2 ..." and your own names) with usage per category and per app (YouTube, TikTok, Facebook, WhatsApp, Telegram, Google, Snapchat ...). Teach it new domains from the dashboard.
- **Ad / tracker / site blocking** (built-in 18 KB list + your own hosts file), a choice of filtering DNS (Cloudflare Family, AdGuard ...), an **Internet off** switch, usage by hour, when each app was used, top sites per user, memory and flash figures.
- **Speed limits** for the whole network, a default per user, and per user.
- **Update over WiFi**: send `ota-firmware.bin` or `ota-ui.bin` from System > Update. A bad firmware rolls back by itself.
- **Domain log** of the latest lookups, per client.
- **Ping** from the repeater, without freezing anything.
- **Telegram bot** for status, block, unblock, whitelist, quota and ping.

### Safety

- **Login on everything.** The dashboard, the API and the live WebSocket all need a signed-in session (password, lock-out after 5 wrong tries), and the page warns you until you change the default.
- **Settings are validated** before they are saved, so a bad password cannot lock you out.
- **Recovery button.** Hold **BOOT** for 5 seconds to wipe the saved settings.
- **Cross-site protection** on every state-changing call.

## How it works

```
Phone / laptop  ──WiFi──▶  ESP32 soft-AP ──▶ lwIP (NAT) ──▶ ESP32 station ──WiFi──▶ Router ──▶ Internet
                                   │
                  hook on the AP network interface
                  ├─ frames from a device  → upload   count by source MAC
                  ├─ frames to a device    → download count by destination MAC
                  └─ blocked device?       → frame dropped
                                   │
   quota table (RAM, locked) ──▶ flash every 1 min     ──▶ dashboard + Telegram
   DNS proxy task   ── logs every lookup, forwards it to the upstream DNS
   Telegram task    ── commands from the owner chat only
```

The full picture is in the [Architecture overview](docs/architecture/overview.md).

## Install

> Version `0.4.0-beta`. Requires a classic **ESP32** board with 4 MB flash.

### 1. Get the firmware

**Download:** open the latest [Actions run](https://github.com/CodeVortax/esp32-wifi-repeater-manage/actions) and take the `firmware` artifact, or a file from [Releases](https://github.com/CodeVortax/esp32-wifi-repeater-manage/releases). It is **one file** that already contains the web UI.

**Or build it yourself:**

```bash
git clone https://github.com/CodeVortax/esp32-wifi-repeater-manage
cd esp32-wifi-repeater-manage
pip install -U platformio
./build.sh merge        # -> dist/esp32dev-merged.bin
```

On Termux, `./build.sh bin` also copies the file to your Downloads folder.

### 2. Flash it

Write the single file at offset `0x0`, from Chrome with [ESP Web Tools](https://web.esphome.io/), or:

```bash
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x0 esp32-wifi-repeater.bin
```

More ways, including from a phone, in the [Flashing guide](docs/guides/flashing.md).

### 3. First run

1. Join the WiFi **`AOSP`** (password `12345678`) and open **http://192.168.4.1**.
2. Log in with user `admin`, password `admin1234`.
3. In **Settings**, enter your home WiFi, a new repeater password and a **new dashboard password**. The ESP32 reboots and joins your router.

Details in the [Setup guide](docs/guides/setup.md).

## The dashboard

| Area | What it does |
|---|---|
| **Stat tiles** | Devices online, data used today, upstream WiFi and signal, uptime and free memory. |
| **Device cards** | Usage bar, online state, **Block**, **Whitelist**, **Set limit**. Tap a name to label a device (kept in your browser). |
| **Domains** | The latest lookups, newest first. |
| **Ping** | Test the upstream link from the repeater itself. |
| **Settings** | Both WiFi networks, the dashboard password and Telegram. Leave a field empty to keep it. |

## Telegram commands

Only the chat ID you saved can control the repeater.

| Command | Does |
|---|---|
| `/status` | Every device with usage and state |
| `/block <mac>` · `/unblock <mac>` | Cut off or restore a device |
| `/whitelist <mac>` · `/unwhitelist <mac>` | Exempt a device from the quota |
| `/setquota <mac> <mb>` | Daily limit in MB (`0` = unlimited) |
| `/ping <host>` | Ping from the repeater |

## Good to know

- **Speed.** NAT on a classic ESP32 is limited, usually a few Mbit/s up to about 20. For more, use an ESP32-S3.
- **Counting** is exact at the IP layer and includes protocol overhead. It is built for quotas, not for billing.
- **DNS over HTTPS** bypasses the domain log: a browser that resolves names over HTTPS never asks the ESP32.
- **Secrets are stored in flash as plain text.** Fine at home; do not hand the board to strangers.
- **Beta.** Counting hooks into the ESP32's network stack. Check the serial log for `AP traffic hooks installed` after boot, and open an issue if the counters stay at zero on your board.

## Tech stack

- ESP32, Arduino-ESP32 3.3 on ESP-IDF 5.5 via [pioarduino](https://github.com/pioarduino/platform-espressif32), pinned
- ESPAsyncWebServer + AsyncTCP, ArduinoJson 7, UniversalTelegramBot
- lwIP NAPT, NVS (`Preferences`), SPIFFS for the web UI
- One dependency-free HTML file for the dashboard (no CDN, works offline)
- GitHub Actions builds the merged image on every push and pull request

## Project layout

```
src/
  main.cpp            Setup order and the loop
  wifi_repeater.*     AP + STA, NAT on "got IP", DNS offer
  quota_manager.*     Per-device counting, quota, block, whitelist, flash
  dns_monitor.*       DNS proxy task and domain log
  web_server.*        REST API, WebSocket, login
  telegram_bot.*      Bot task
  diagnostics.*       Async ping
  settings.*          Validated settings in flash
include/config.h      First-boot defaults
data/index.html       The dashboard
scripts/merge_bins.py `pio run -t webflash`: one flashable file
docs/                 Architecture and guides (start at docs/README.md)
```

## Documentation

| Topic | Page |
|---|---|
| Index of all docs | [docs/README.md](docs/README.md) |
| Architecture overview | [docs/architecture/overview.md](docs/architecture/overview.md) |
| How quota counting works | [docs/architecture/quota.md](docs/architecture/quota.md) |
| Setup guide | [docs/guides/setup.md](docs/guides/setup.md) |
| Building | [docs/guides/building.md](docs/guides/building.md) |
| Flashing | [docs/guides/flashing.md](docs/guides/flashing.md) |
| Web API | [docs/guides/web-api.md](docs/guides/web-api.md) |
| Troubleshooting | [docs/guides/troubleshooting.md](docs/guides/troubleshooting.md) |

## Privacy

No cloud and no analytics. The only outside connections are the ones you configure: your router, NTP for the clock, DNS forwarding, and Telegram if you set a bot token.

## Contributing

Issues and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Free software under the **GNU Affero General Public License v3.0 only** (`AGPL-3.0-only`). See [LICENSE](LICENSE).

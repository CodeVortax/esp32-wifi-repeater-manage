# Changelog

All notable changes to **ESP32 WiFi Repeater Manager** (`CodeVortax/esp32-wifi-repeater-manage`) are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and release notes in this file are automatically attached to GitHub Releases by `.github/workflows/build-firmware.yml`.

## [Unreleased]

- Ongoing multi-node calibration and companion UI refinements.

## [2.0.0] - 2026-11-23

### Added
- **Auto Channel & Low-Latency Optimizer** (`src/wifi_channel_opt.cpp`): Scans 2.4 GHz Channels `1–13` for RSSI overlap, steers active repeaters to cleanest non-overlapping `HT20` channels (`1`, `6`, `11`), disables modem power-save (`WIFI_PS_NONE`), and adds an NAPT low-latency QoS fast-path for DNS/ICMP/Gaming/TCP-ACK.
- **Monolithic Static Block Storage (`nvblock`)** (`src/static_block_store.h`, `src/static_block_store.cpp`): Replaces arbitrary 16-user slots with a single 512 KB wear-leveled CRC32 arena supporting up to 128 MACs via FNV-1a open addressing.
- **Unified Single-Firmware Flashing (`0x10000`)** (`partitions.csv`, `scripts/embed_ui.py`): Embeds gzipped Web UI into `PROGMEM` inside `firmware.bin` (`factory` @ `0x10000`, `0x360000`) so updates flash only `*-firmware-0x10000.bin` without touching bootloader, partition table, or `nvblock`.
- **Precise Time-Range Analytics**: Query traffic & latency over exact `[From, To]` timestamps at 5-minute granularity per user or network-wide, with CSV export.
- **Monthly Plan Cycle & 48-Hour Grace Retention** (`src/monthly_plan.cpp`): Resets active user usage on monthly rollover, preserves raw cycle slices for 48 hours (2 days) before pruning, and keeps permanent monthly summaries (per-user total, `#1` most-used app, and total plan usage).
- **UI-Password AES-256-GCM Backups & Telegram Auto-Backup** (`src/crypto_backup.cpp`, `src/utils/cryptoBackup.ts`): Client/ESP32 PBKDF2-HMAC-SHA256 + AES-256-GCM `.vortaxbak` backup & restore keyed to the dashboard UI password, plus Telegram Bot delivery on rollover or schedule.
- **Full Localization (`EN` / `AR` RTL / `FR`) & Documentation**: Translated UI strings across all tabs, added `README.md`, `docs/ARCHITECTURE_AND_FLASHING.md`, and multi-language desktop/mobile screenshots in `docs/screenshots/`.
- **Automated GitHub Releases** (`.github/workflows/build-firmware.yml`): Publishes `*-firmware-0x10000.bin`, `*-first-flash-0x0.bin`, SHA256 checksums, and `.elf.gz` using notes extracted from `CHANGELOG.md`.

### Changed
- **Zero-Mock Live Bridge Mode**: Removed all seeded demo data and simulated traffic generators; `/api/bridge/*` connects to live ESP32 endpoints and persists state in `data/repeater-store.json`.
- **Housekeeping**: Updated `package.json` metadata and `.gitignore` rules (`data/`, `*.vortaxbak`, `.pio/`).

### Fixed
- **Post-Login RAM Crash & Task WDT Reset** (`src/ram_wdt_guard.cpp`): Replaced 8 concurrent login JSON fetches with a single coalesced chunked `/api/snapshot` stream, static TX buffer, cooperative WDT yields, and non-resetting `RTC_NOINIT_ATTR` fault logging.
- **Mobile Viewport Overflow & Bottom Nav Clipping** (`src/index.css`, `.table-wrap`): Wrapped wide tables cleanly inside cards and adjusted `nav.bottom .nav` sizing (`flex: 1 1 0; min-width: 0`) so all 7 navigation tabs fit cleanly on 360px–390px screens.

# Changelog

Versions follow `FW_VERSION` in `include/config.h`. All releases are betas.

## Unreleased
- **Adult filter ("kill porn")**: built-in list of 118,887 names (330 KB, in flash) plus keyword and domain-ending rules. Optional lock: once locked there is no off switch in the dashboard or the API; only a factory reset (hold BOOT) or a re-flash removes it, and no Allow rule can lift it.
- **Kill an app for one user** (YouTube, TikTok, Facebook, ...): its names are answered with nothing and its traffic is dropped. Set in the user's Edit dialog; the user sees it on their usage page.
- **Usage notices**: a notice at 80% of the daily limit and at the limit; users open `http://usage.lan` (or `/my_usage`) to see their own usage in 6 languages. `usage.lan` now always opens the user page, never the admin login.
- Dashboard updates: the page stops polling while uploading and shows the real error; the board unmounts its files first and restarts itself if a dashboard update fails.
- The board's own radio addresses are never listed as a user; a "Forget" button removes an offline device.

## 0.4.0-beta

### Added
- **Ad, tracker and site blocking by DNS.** A built-in list of 6,572 names (Peter Lowe + AdAway, plus a few seen on real phones) costs only 18 KB of flash (hash set, Golomb-Rice coded, no RAM). Add your own with a **hosts file** (`0.0.0.0 example.com`, `||example.com^`, plain names, `@@example.com` to always allow) or the one-tap Block buttons. Blocked names get `0.0.0.0` straight from the repeater. The file is kept in NVS, so a dashboard update never deletes it. Regenerate the list with `python3 scripts/gen_blocklist.py`; CI checks that it is current.
- **DNS provider**: router (default), Cloudflare, Cloudflare Security / Family, Google, Quad9, AdGuard, AdGuard Family, OpenDNS FamilyShield, CleanBrowsing Family, or two custom addresses. If the first server is silent the second is used.
- **Usage by hour** for each user and for everybody, and **when each app was used** (hours in which it moved at least 64 KB). Saved every 10 minutes.
- **Most used sites** per user today (by lookups, grouped by site).
- **Internet off** for everybody, like unplugging the WAN (the dashboard and trusted devices keep working).
- **Memory and storage** on the System page: RAM, firmware slot, dashboard files, saved data.
- **WiFi range** settings: transmit power level, 20 MHz channels on both sides. Per-user signal strength on the Users page.

### Fail-safes
- A repair page (sign in + upload) is shown when the dashboard files are missing or damaged.
- Counter saves that fail (flash full or worn) are retried, counted and shown as a warning.
- Hosts file and uploads are checked against free space and memory before they are written; the old file stays if anything fails.
- If RAM stays critically low for a minute the repeater saves its counters and restarts.

### Fixed
- Traffic to addresses shared by thousands of sites (Cloudflare, Fastly) was credited to whichever app asked last, which inflated "AI assistants" and "Other music". Those addresses are no longer remembered, so that traffic shows as Other.
- Phone-maker, clock and connectivity-check names (Oppo/Realme, MediaTek, NTP, Android time) are recognised instead of landing in Other.

## 0.3.0-beta

### Added
- **Router-style dashboard with a login screen.** Sign in with the dashboard password (session cookie, 12 h, lock-out after 5 wrong tries per address). Pages: Overview, Users, Apps, Speed, Settings, System. Arabic / English, right-to-left.
- **Named users.** Every device is "User 1, 2, ..." in the order it first appeared. Rename it from the dashboard; the name is stored on the repeater, not in your browser.
- **Usage per app and category** (video, social, messaging, browsing, music, gaming, downloads, other), per user, from DNS names, CNAMEs, remembered IPs and Telegram's address ranges. Unknown domains are listed so you can teach the repeater (custom rules, kept in flash).
- **Speed limits**: for the whole network, a default per user, and per user (Mbps, separate down / up).
- **Update over WiFi** (System > Update) for the firmware and for the dashboard files, with automatic roll-back if the new firmware fails to start 3 times.
- Host-side unit tests (`make -C test`, with sanitizers) run in CI.

### Changed
- Counters are saved **every minute** (only when something changed), as atomic checksummed blobs, and once more before every restart.
- New partition table (`partitions.csv`): two 1.75 MB app slots, 320 KB SPIFFS. **The first install of this version needs a full flash at 0x0 and resets saved settings once.** Later versions can be installed from the dashboard.
- HTTP Basic auth was replaced by the login page and a session cookie.

### Known limits
- App recognition is DNS based. Phones with Private DNS / DoH and VPN users hide their lookups; that traffic is counted as "Other".
- The app table was assembled from public domain lists and is only partly verified on real traffic. Use Apps > "Domains I do not know yet" to fix gaps.

## 0.2.0-beta

### Fixed
- **Quota counting.** It now counts at the network-stack level, in both directions, per device. Before, it used the radio's promiscuous mode, which missed downloads and counted every other network on the channel.
- **Blocking kicked everyone.** A block now drops only that device's traffic and deauthenticates only that device.
- **Reboots wiped everything.** Devices, limits, whitelist, blocks and counters are now saved and restored. Flash writes were cut to once per 5 minutes.
- **Midnight reset** used a fixed UTC+2 and missed Egypt's daylight saving. It now uses a POSIX time zone, survives reboots, and no longer blocks the loop while the clock is unset.
- **The repeater never started** when the router took longer than 20 seconds. NAT now turns on whenever the station gets an address.
- **Slow browsing for everyone.** DNS and Telegram now run in their own tasks, and ping is asynchronous so it cannot stall the web server.
- **Data races.** All shared state is copied under a lock or read as snapshots.
- **Ping** accepted only IP addresses. It now resolves hostnames.
- **Saving the same settings rebooted the device.** It now reboots only when something changed.
- **README build instructions** pointed at a file that is never produced.

### Security
- Login (HTTP Basic) on the dashboard, API and WebSocket, and a warning until the default password is changed.
- Cross-site request protection on every POST.
- Domain names are rendered as text, which fixes a stored XSS from DNS names.
- Settings are validated, and a bad AP password falls back to the defaults.

### Added
- New dashboard: stat tiles, online state, usage bars, device names, dialogs instead of `prompt()`, light and dark themes, Arabic and English with right-to-left.
- System info in the dashboard (upstream signal, channel, uptime, memory).
- DHCP hands out the ESP32 as DNS so the domain log sees every lookup.
- Quota `0` means unlimited. `/whitelist`, `/unwhitelist` in Telegram.
- Recovery: hold BOOT for 5 seconds.
- `./build.sh merge` and `./build.sh bin` (copies to Downloads on Termux).
- Documentation in `docs/`, and an Arabic README.

### Changed
- **Build.** The merged image is now made by an explicit `webflash` target instead of a hidden post-action that started a second PlatformIO. Offsets and flash size are read from the partition table. The platform is pinned to pioarduino `55.03.311`. The unused NTPClient library and the custom sdkconfig were removed.
- **CI** runs on every push to `master` and on pull requests, not only on tags.
- Telegram polling every 3 seconds.
- Licence: AGPL-3.0-only.

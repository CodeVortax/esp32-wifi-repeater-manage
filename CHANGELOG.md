# Changelog

Versions follow `FW_VERSION` in `include/config.h`. All releases are betas.

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

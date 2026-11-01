# Quota counting

## Where bytes are counted

The firmware replaces two function pointers on the lwIP network interface of the soft-AP (`WIFI_AP_DEF`):

| Hook | Direction | Device identified by |
|---|---|---|
| `netif->input` | device → ESP32 (upload) | source MAC, bytes 6-11 of the Ethernet frame |
| `netif->linkoutput` | ESP32 → device (download) | destination MAC, bytes 0-5 |

All NAT-ed traffic crosses this interface, so counting is complete in both directions and covers only devices really associated with the repeater. Broadcast and multicast frames are ignored.

Earlier versions used the radio's promiscuous mode. That only sees frames the ESP32 **receives**, so downloads were missed, and it also saw every other network on the same channel.

## What a count means

Counts are Ethernet frame sizes, so they include IP and TCP/UDP headers. Expect them to be a few percent above what a phone's own counter shows.

## Quota and blocking

When a device's total for the day reaches its limit, the hook sets `blocked`. From then on:

1. frames **from** the device are dropped before they reach the IP stack,
2. frames **to** the device are dropped before they reach the radio,
3. the device is deauthenticated once, so it notices.

It may reconnect, but it stays blocked and gets no traffic. A limit of `0` means unlimited, and whitelisted devices are never counted against a limit.

| Action | Effect |
|---|---|
| **Unblock** | Lifts the block and ignores the quota until the next reset |
| **Set limit** | A new limit above current use lifts an automatic block |
| **Whitelist** | Lifts the block and exempts the device |
| **Midnight** | Resets today's counters and lifts every block |

## The device table

`MAX_TRACKED_DEVICES` (default 10) slots. When it is full, the least recently seen slot is recycled, preferring devices without a whitelist or block. Everything is kept in flash and restored at boot.

## Midnight

`TZ_INFO` in `include/config.h` is a POSIX time-zone string. The default is Egypt (`EET-2EEST,M4.5.5/0,M10.5.4/24`). The day of the last reset is saved, so a reboot that spans midnight still resets. The reset waits until NTP has set the clock.

## Apps, users and speed (0.3.0)

- **Users.** The device table is keyed by MAC. Each device gets a number in order of first appearance and an optional name (32 bytes) stored on the repeater.
- **App recognition** (`src/apps.cpp`, `src/dns_parse.cpp`). The repeater is each client's DNS server. A looked-up name is matched against a suffix table (longest suffix wins), CNAME targets too, and the A records of the answer are remembered (IP to app, 6 h). Counted frames look up their remote IP. Telegram's fixed address ranges are matched directly. Anything else is "Other". Custom rules win over the table.
- **Limits of that idea.** DNS over HTTPS / Private DNS and VPNs hide the names; shared CDN addresses can be attributed to the wrong app for a moment (generic Google addresses never overwrite a specific app).
- **Speed limits** (`src/shaper.h`). Token buckets per direction: network-wide, then per user (own cap or the default). Packets over the rate are dropped, so TCP backs off. Whitelisted devices and traffic to the repeater itself are exempt.
- **Saving.** Dirty counters are written once a minute as one CRC-checked blob; the old blob stays valid if power fails mid-write. At most 1440 writes a day, far below the flash's endurance.
- **Tests.** `make -C test` runs the pure C++ parts on a PC with sanitizers (also in CI).

## What happens at the limit, and extra data for today

Each device has a quota action (default **block**, the behaviour of every earlier version, so nothing changes for existing installs):

| Action | Effect when `used >= limit + today's bonus` |
|---|---|
| Block | As above: the device is cut off |
| Slow down | Both directions are limited to 30 KB/s (30 000 bytes/s) |
| Custom | The same, with the device's own speed (1-65000 KB/s, decimal) |
| Notify only | Nothing is restricted; the event is only logged |

The slow-down is not a second limiter. `account()` asks `quota_effectiveRate()` (`src/quota_policy.h`) for the rate and feeds it to the same per-device token bucket. The quota speed **replaces** the normal cap while the quota is used up; if the normal cap is already lower, the lower one stays (running out of quota never makes a device faster). The global "everybody together" bucket still applies on top. Exhaustion is computed from the byte counter on every frame, not stored as a flag, so a bonus, a new limit or the daily reset takes effect on the next frame, and reconnecting or changing IP changes nothing (the device is keyed by MAC).

**Extra data for today** (`/api/bonus`) adds to the allowance without touching the permanent limit or the used bytes. Bonuses add up. They are saved in their own small blob (`bon1`) together with the quota-day key, and are only read back when that key equals the counters' day, so a bonus cannot outlive the day, even across a reboot. The daily reset clears every bonus. A bonus lifts an automatic (quota) block when the device is no longer over its allowance; a block set by the admin stays until the admin unblocks. The 80% notice is measured against limit + bonus.

Units: quota and bonus are MB (1 048 576 bytes, as the daily limit always was); the slow-down speed is KB/s, decimal. The existing speed caps stay in kbit/s.

Limits: the byte counters reach flash once a minute, so a power cut can lose up to a minute of counted traffic (unchanged from before).

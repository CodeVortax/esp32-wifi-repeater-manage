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

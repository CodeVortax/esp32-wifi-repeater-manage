# Architecture overview

## Modules

| File | Owns |
|---|---|
| `main.cpp` | Start-up order, the loop, the BOOT-button recovery |
| `wifi_repeater.*` | Soft-AP and station, NAT, offering the ESP32 as DNS |
| `quota_manager.*` | The device table, per-device counting, quota, block, whitelist, flash |
| `dns_monitor.*` | A DNS proxy on UDP 53 and the domain log |
| `web_server.*` | REST API, WebSocket, login, serving the dashboard |
| `telegram_bot.*` | The Telegram bot |
| `diagnostics.*` | Ping |
| `settings.*` | Saved network, login and Telegram settings, with validation |
| `data/index.html` | The dashboard, served from SPIFFS |

## Start-up order

```
settings_init()      defaults are written to flash on the very first boot only
wifiRepeater_init()  AP up, station connecting in the background
configTzTime()       time zone + NTP
quota_init()         restore devices from flash, hook the AP network interface
dnsMonitor_init()    task
webServer_init()     async server, login
telegramBot_init()   task, only when a real token and chat ID are saved
```

`quota_init()` must come after `wifiRepeater_init()`: it hooks the AP network interface, which only exists once the AP is up.

## Tasks and threads

| Context | Does | Shared data it touches |
|---|---|---|
| Wi-Fi / lwIP | Runs the counting hook for every frame | Device table (short spinlock) |
| `loop()` task | Midnight reset, deauth, flash writes, WebSocket push, recovery button | Device table |
| `async_tcp` | HTTP handlers | Reads snapshots; never blocks |
| `dns` task | Proxy and domain log | Log ring (spinlock) |
| `telegram` task | TLS polling and commands | Snapshots |
| `ping` task | Resolves the name, starts the ping | Ping state (spinlock) |

Rules the code follows: **no allocation and no `Serial` inside a spinlock**, **nothing slow inside the web handlers**, and other tasks read **copies** of the device table.

## Storage

| Where | What | When it is written |
|---|---|---|
| NVS namespace `netcfg` | WiFi names and passwords, dashboard password, Telegram | When you save Settings |
| NVS namespace `quota` | Known devices, limits, flags, counters, the day of the last reset | Every 5 minutes if counters changed, and soon after any change you make |
| SPIFFS | `index.html` | When you flash |

## Partitions

`min_spiffs.csv`: two 1.9 MB app slots (room for OTA later), a 128 KB SPIFFS for the dashboard and a small core-dump area. The whole table fits a 4 MB flash.

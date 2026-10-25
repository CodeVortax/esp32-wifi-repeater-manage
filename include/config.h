#pragma once

#define FW_VERSION               "0.4.0-beta"

// These are only the FIRST-BOOT defaults. After the first boot everything
// below marked (NVS) lives in flash and is changed from the Settings page.
// To re-apply new defaults you must erase flash or hold BOOT (GPIO0) for 5 s.

// ---------------- WiFi STA (upstream router you're extending) (NVS) ----------------
#define STA_SSID                 "YOUR_HOME_WIFI"
#define STA_PASSWORD             "YOUR_HOME_PASSWORD"

// ---------------- WiFi AP (the repeater network devices connect to) (NVS) ----------------
#define AP_SSID                  "AOSP"
#define AP_PASSWORD              "12345678"
#define AP_MAX_CONN              8

// ---------------- Dashboard login (NVS) ----------------
// HTTP Basic auth. The dashboard warns you until the password is changed.
#define WEB_ADMIN_USER           "admin"
#define WEB_ADMIN_PASSWORD       "admin1234"
#define WEB_SERVER_PORT          80

// Login page: a session cookie replaces the browser's Basic-auth popup.
#define SESSION_MAX              4                       // logged-in browsers at once
#define SESSION_TTL_MS           (12UL * 3600UL * 1000UL) // idle time before you must log in again
#define LOGIN_MAX_FAILS          5                       // wrong passwords in a row, per device...
#define LOGIN_LOCK_MS            60000UL                 // ...then that device waits this long

// ---------------- Users, quota and speed ----------------
#define DAILY_QUOTA_MB_DEFAULT   1024UL     // 1 GB/day per device; 0 = unlimited
#define MAX_TRACKED_DEVICES      16
#define NVS_QUOTA_NAMESPACE      "quota"
// Counters are written at most once a minute, and only when something changed.
// Names, limits and speeds are written at once (they are tiny and rare).
#define QUOTA_SAVE_INTERVAL_MS   (60UL * 1000UL)
#define MAX_SPEED_KBPS           1000000UL  // 1 Gbit/s, the largest value the API accepts

// ---------------- App recognition ----------------
#define MAX_CUSTOM_RULES         24
#define UNKNOWN_DOMAIN_MAX       40

// ---------------- Ad / site blocking ----------------
// Your hosts file is kept in flash next to the settings (not in the dashboard files, so a
// dashboard update never deletes it). Limits keep RAM and flash use small and predictable.
#define MAX_USER_BLOCKS          1500        // names in your hosts file (6 KB of RAM while loaded)
#define MAX_USER_ALLOWS          100
#define HOSTS_FILE_MAX           12288       // bytes

// ---------------- Updates over WiFi ----------------
// After a firmware update the new version must stay up this long to count as
// healthy. If it reboots 3 times before that, the previous version is restored.
#define OTA_HEALTHY_MS           60000UL

// ---------------- Time / daily reset ----------------
// POSIX TZ string. Egypt: UTC+2, DST from the last Friday of April to the
// last Thursday of October. Change this for another country.
#define NTP_SERVER               "pool.ntp.org"
#define TZ_INFO                  "EET-2EEST,M4.5.5/0,M10.5.4/24"

// ---------------- DNS monitor ----------------
#define DNS_FALLBACK_UPSTREAM    "1.1.1.1"
#define DNS_LOG_SIZE             40

// ---------------- Telegram (NVS) ----------------
#define TELEGRAM_BOT_TOKEN       "YOUR_BOT_TOKEN"
#define TELEGRAM_CHAT_ID         "YOUR_CHAT_ID"
#define TELEGRAM_POLL_MS         3000

// ---------------- Recovery ----------------
// Hold the BOOT button (GPIO0) this long while running to wipe the saved
// network/login settings and fall back to the defaults above.
#define FACTORY_RESET_PIN        0
#define FACTORY_RESET_HOLD_MS    5000

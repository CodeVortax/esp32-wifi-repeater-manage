#pragma once

// ---------------- WiFi STA (upstream router you're extending) ----------------
#define STA_SSID              "YOUR_HOME_WIFI"
#define STA_PASSWORD          "YOUR_HOME_PASSWORD"

// ---------------- WiFi AP (the repeater network devices connect to) ----------------
#define AP_SSID               "AOSP"
#define AP_PASSWORD           "12345678"
#define AP_MAX_CONN           8

// ---------------- Quota ----------------
#define DAILY_QUOTA_MB_DEFAULT   1024UL     // 1GB/day per device by default
#define MAX_TRACKED_DEVICES      10
#define NVS_NAMESPACE            "quota"

// ---------------- Time / daily reset ----------------
#define NTP_SERVER               "pool.ntp.org"
#define GMT_OFFSET_SEC           (2 * 3600)  // Egypt, UTC+2
#define DAYLIGHT_OFFSET_SEC      0

// ---------------- Telegram ----------------
#define TELEGRAM_BOT_TOKEN       "YOUR_BOT_TOKEN"
#define TELEGRAM_CHAT_ID         "YOUR_CHAT_ID"
#define TELEGRAM_POLL_MS         2000

// ---------------- Web server ----------------
#define WEB_SERVER_PORT          80

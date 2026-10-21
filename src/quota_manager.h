#pragma once
#include <Arduino.h>

struct DeviceUsage {
    uint8_t  mac[6];
    char     macStr[18];
    uint64_t bytesToday;
    uint64_t bytesAllTime;
    uint32_t dailyLimitMB;   // 0 = unlimited
    bool     blocked;        // manually or auto (quota) blocked
    bool     whitelisted;    // guest whitelist -> unlimited, never blocked
    bool     active;         // slot in use
    bool     online;         // filled in by quota_snapshot(): associated right now
    bool     kickPending;    // internal: deauth this station from the main loop
    bool     quotaOverride;  // manually unblocked: ignore the quota until the daily reset
    uint32_t lastSeenMs;     // millis() of the last frame
};

void quota_init();           // call after wifiRepeater_init()
void quota_loop();           // call from loop(): midnight reset, deauth, flash save

// Thread-safe copy of the device table (only active slots). Returns count.
int  quota_snapshot(DeviceUsage *out, int maxCount);

// All of these take a MAC like "AA:BB:CC:DD:EE:FF" (":" or "-", any case).
bool quota_setLimit(const char *macStr, uint32_t mb);
bool quota_block(const char *macStr);
bool quota_unblock(const char *macStr);
bool quota_setWhitelist(const char *macStr, bool enabled);

void quota_resetDaily();
bool quota_hooksInstalled();

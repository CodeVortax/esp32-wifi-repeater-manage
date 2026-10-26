#pragma once
#include <Arduino.h>
#include "apps.h"
#include "shaper.h"

#define SPEED_DEFAULT    0xFFFFFFFFu   // "use the default speed" (0 = unlimited)
#define NAME_MAX_BYTES   32

struct DeviceUsage {
    uint8_t  mac[6];
    char     macStr[18];
    char     name[NAME_MAX_BYTES + 1];  // "" = not named yet (the UI shows "User <userNo>")
    uint8_t  userNo;                    // 1, 2, 3... in the order devices were first seen
    uint32_t ip;                        // last IPv4 seen, in the byte order of an IPv4 header
    uint64_t bytesToday;
    uint64_t bytesAllTime;
    uint64_t catToday[CAT_COUNT];       // filled in by quota_snapshot()
    uint32_t dailyLimitMB;              // 0 = unlimited
    uint32_t downKbps;                  // this user's speed cap, 0 = unlimited, SPEED_DEFAULT = follow the default
    uint32_t upKbps;
    bool     blocked;                   // manually or auto (quota) blocked
    bool     whitelisted;               // never blocked, never limited
    bool     active;                    // slot in use
    bool     online;                    // filled in by quota_snapshot(): associated right now
    int8_t   rssi;                      // filled in by quota_snapshot(): signal in dBm, 0 when offline
    bool     kickPending;               // internal: deauth this station from the main loop
    bool     quotaOverride;             // manually unblocked: ignore the quota until the daily reset
    bool     warnSent;                  // the 80% notice was already triggered today
    bool     warnPending;               // the user has not seen it yet
    uint32_t warnSince;                 // millis() when it was triggered
    uint32_t lastSeenMs;                // millis() of the last frame
    Bucket   dl, ul;                    // internal: speed limiter state
};

struct SpeedConfig {
    uint32_t globalDownKbps;            // everybody together, 0 = unlimited
    uint32_t globalUpKbps;
    uint32_t defaultDownKbps;           // per user, for users without their own cap
    uint32_t defaultUpKbps;
};

void quota_init();           // call after wifiRepeater_init()
void quota_loop();           // call from loop(): midnight reset, deauth, flash save
uint32_t quota_saveFailures();   // saves that failed since boot (flash full or worn)
void quota_saveNow();        // flush everything to flash (before a reboot)

// Thread-safe copy of the device table (only active slots). Returns count.
// The struct is ~250 bytes: copy into heap memory from small task stacks.
int  quota_snapshot(DeviceUsage *out, int maxCount);

// Per-app bytes today for one device (macStr == nullptr: all devices together).
// out has APP_COUNT entries. Returns false when the device is unknown.
bool quota_appBytes(const char *macStr, uint64_t *out);

// "name" or "User N" for the device that currently holds this IPv4 address. false: unknown.
bool quota_labelForIp(uint32_t ip, char *out, size_t outSize);

// All of these take a MAC like "AA:BB:CC:DD:EE:FF" (":" or "-", any case).
bool quota_setLimit(const char *macStr, uint32_t mb);
bool quota_block(const char *macStr);
bool quota_unblock(const char *macStr);
bool quota_setWhitelist(const char *macStr, bool enabled);
bool quota_setName(const char *macStr, const char *name);                 // "" clears the name
bool quota_setDeviceSpeed(const char *macStr, uint32_t downKbps, uint32_t upKbps);

// When was it used? hoursKB[h][category] = KB moved in local hour h today; appMask[app] has bit h set
// when that app moved at least 64 KB in hour h. macStr == nullptr: all devices together.
bool quota_hourly(const char *macStr, uint32_t hoursKB[24][CAT_COUNT], uint32_t *appMask);

// "Internet off" for everybody, like pulling the WAN cable: every device is cut off from the
// internet (the dashboard, which is the repeater itself, still works) except trusted devices.
bool quota_internetOff();
void quota_setInternetOff(bool off);

SpeedConfig quota_getSpeedConfig();
void        quota_setSpeedConfig(const SpeedConfig &cfg);

void quota_resetDaily();
bool quota_hooksInstalled();
bool quota_forget(const char *macStr);              // remove a device that is not connected

// ---- what a user sees about himself (looked up by the address he connects from) ----
struct ClientInfo {
    char     name[NAME_MAX_BYTES + 1];
    uint8_t  userNo;
    char     macStr[18];
    uint64_t bytesToday;
    uint32_t limitMB;
    uint32_t downKbps, upKbps;          // SPEED_DEFAULT already resolved to the default
    bool     blocked;                   // paused (by the quota or by the admin)
    bool     quotaHit;                  // ... because the daily limit was reached
    bool     whitelisted;
    bool     warnPending;
};
bool quota_clientInfo(uint32_t ip, ClientInfo &out);
void quota_ackNotice(uint32_t ip);                    // "OK, continue" on the 80% notice
// Should this client's DNS answer lead to the notice page? Blocked / internet off: every name.
// A fresh 80% notice: only the names phones use to detect a login page (isCheck).
bool quota_wantsNotice(uint32_t ip, bool isCheck);
bool quota_noticeEnabled();
void quota_setNoticeEnabled(bool on);
bool quota_isTrustedIp(uint32_t ip);    // is the device holding this address marked trusted?

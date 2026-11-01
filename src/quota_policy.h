#pragma once
// What happens when a device uses up its daily quota, and the "extra data for today" bonus.
// Pure C++ (no Arduino) so it can be unit-tested on a PC: make -C test
//
// Units, so nothing is confused:
//   quota and bonus  MB  = 1 048 576 bytes (the unit the dashboard has always used for the daily limit)
//   slow-down speed  KB/s = 1000 bytes per second (decimal): 30 KB/s = 30 000 bytes/s
//   the normal speed caps elsewhere are kbit/s; the caller converts them to bytes/s first
#include <stdint.h>

enum QuotaAction : uint8_t {
    QA_BLOCK  = 0,     // cut the device off (the behaviour of every earlier version, so it stays the default)
    QA_SLOW   = 1,     // limit to QUOTA_SLOW_KBPS_DEFAULT
    QA_CUSTOM = 2,     // limit to the device's own slow-down speed
    QA_NOTIFY = 3,     // only note it; no restriction
    QA_COUNT
};

#define QUOTA_SLOW_KBPS_DEFAULT  30        // KB/s
#define QUOTA_SLOW_KBPS_MIN      1
#define QUOTA_SLOW_KBPS_MAX      65000     // fits the 16 bits kept in the saved record
#define QUOTA_BONUS_MAX_MB       1000000   // 1 TB of extra data for one day is already absurd

static inline bool quota_actionValid(uint32_t a) { return a < QA_COUNT; }

// Today's allowance in bytes: the permanent limit plus today's bonus. 0 limit = unlimited.
static inline uint64_t quota_effectiveBytes(uint32_t limitMB, uint32_t bonusMB) {
    return ((uint64_t)limitMB + bonusMB) * 1048576ULL;
}

// Evaluated from the byte counter on every frame (no stored "slowed" flag), so a bonus, a new limit
// or the daily reset takes effect on the very next frame and a reboot cannot lose the state.
static inline bool quota_exhausted(uint32_t limitMB, uint32_t bonusMB, uint64_t usedBytes) {
    return limitMB > 0 && usedBytes >= quota_effectiveBytes(limitMB, bonusMB);
}

static inline uint32_t quota_remainingMB(uint32_t limitMB, uint32_t bonusMB, uint64_t usedBytes) {
    if (limitMB == 0) return 0;
    uint64_t eff = quota_effectiveBytes(limitMB, bonusMB);
    return usedBytes >= eff ? 0 : (uint32_t)((eff - usedBytes) >> 20);
}

// Slow-down speed in bytes/s for an action (0 when the action does not slow anything down).
static inline uint32_t quota_slowBytesPerSec(uint8_t action, uint32_t customKBps) {
    if (action == QA_SLOW) return QUOTA_SLOW_KBPS_DEFAULT * 1000u;
    if (action == QA_CUSTOM) {
        uint32_t k = customKBps;
        if (k < QUOTA_SLOW_KBPS_MIN) k = QUOTA_SLOW_KBPS_DEFAULT;     // never saved / 0: fall back, never "unlimited"
        if (k > QUOTA_SLOW_KBPS_MAX) k = QUOTA_SLOW_KBPS_MAX;
        return k * 1000u;
    }
    return 0;
}

// The one rate used for one direction of one device (bytes/s, 0 = unlimited).
// Exhausted + slow-down action: the quota speed REPLACES the normal one. If the normal cap is already
// lower than the slow-down speed, the lower of the two wins: running out of quota must never make
// a device faster than the admin made it. It is applied once, to the same token bucket as always.
static inline uint32_t quota_effectiveRate(uint32_t normalBps, bool exhausted, uint8_t action, uint32_t customKBps) {
    if (!exhausted) return normalBps;
    uint32_t slow = quota_slowBytesPerSec(action, customKBps);
    if (slow == 0) return normalBps;
    return (normalBps != 0 && normalBps < slow) ? normalBps : slow;
}

// Adds to today's bonus without overflowing. Returns false when the amount is unusable.
static inline bool quota_bonusAdd(uint32_t &bonusMB, uint32_t addMB) {
    if (addMB == 0 || addMB > QUOTA_BONUS_MAX_MB) return false;
    uint64_t sum = (uint64_t)bonusMB + addMB;
    bonusMB = sum > QUOTA_BONUS_MAX_MB ? QUOTA_BONUS_MAX_MB : (uint32_t)sum;
    return true;
}

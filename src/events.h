#pragma once
#include <Arduino.h>
// Network event history: a small ring buffer in RAM (EVENT_LOG_SIZE entries, 20 bytes each).
// Nothing is written to flash, so it starts empty after a reboot (the "boot" entry says when that was).
// events_add() may be called from any task, but not from the packet hook (it takes a lock and reads the clock).

enum EventType : uint8_t {
    EV_BOOT = 0,
    EV_STA_UP,          // upstream Wi-Fi connected
    EV_STA_DOWN,        // upstream Wi-Fi lost (arg = reason code)
    EV_DEV_JOIN,        // a device joined the repeater (mac)
    EV_DEV_LEAVE,       // a device left (mac)
    EV_QUOTA_HIT,       // a device used up its daily quota (mac, arg = QuotaAction in force)
    EV_QUOTA_RESET,     // daily reset: counters, blocks and bonuses cleared
    EV_BONUS,           // extra data for today added (mac, arg = MB)
    EV_ADMIN_BLOCK,     // blocked by the admin (mac)
    EV_ADMIN_UNBLOCK,   // unblocked by the admin (mac)
    EV_TYPE_COUNT
};

#define EVENT_LOG_SIZE 64

struct EventRec {
    uint32_t epoch;      // wall-clock seconds, 0 while the clock is not set yet
    uint32_t upSec;      // seconds since boot
    uint32_t arg;
    uint8_t  mac[6];
    uint8_t  type;
    uint8_t  pad;
};

void events_add(uint8_t type, const uint8_t *mac = nullptr, uint32_t arg = 0);
// Newest first. Returns the number of entries copied.
int  events_snapshot(EventRec *out, int maxCount);

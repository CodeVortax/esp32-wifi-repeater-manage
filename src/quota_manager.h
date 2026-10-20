#pragma once
#include <Arduino.h>

struct DeviceUsage {
    uint8_t  mac[6];
    char     macStr[18];
    uint64_t bytesToday;
    uint64_t bytesAllTime;
    uint32_t dailyLimitMB;
    bool     blocked;       // manually or auto (quota) blocked
    bool     whitelisted;   // guest whitelist -> unlimited
    bool     active;        // slot in use
};

void quota_init();
void quota_loop();   // call every loop(): checks midnight rollover + enforces limits

// Called by the promiscuous sniffer with every observed frame's src MAC + length
void quota_recordBytes(const uint8_t *mac, uint32_t len);

DeviceUsage* quota_getDevices(int &count);
bool quota_setLimit(const char *macStr, uint32_t mb);
bool quota_block(const char *macStr);
bool quota_unblock(const char *macStr);
bool quota_setWhitelist(const char *macStr, bool enabled);
void quota_resetDaily();
void quota_saveToNvs();
void quota_loadFromNvs();

#pragma once
#include <Arduino.h>
#include <IPAddress.h>

struct DnsLogEntry {
    IPAddress clientIp;
    uint32_t  timestamp;     // millis()
    uint8_t   app;           // APP_* id, APP_OTHER when not recognised
    char      domain[96];
};

// A name nobody claimed, with how often it was asked for. Shown in the dashboard so a
// rule can be added without a new firmware.
struct UnknownDomain {
    char     domain[64];
    uint32_t count;
    uint32_t clientIp;       // last client that asked, IPv4 header byte order
};

void dnsMonitor_init();      // starts a background task; loop() needs nothing

// Copies the log, newest first. Returns the number of entries copied.
int  dnsMonitor_snapshot(DnsLogEntry *out, int maxCount);

// Most-asked unrecognised names first. Returns the number copied.
int  dnsMonitor_unknownSnapshot(UnknownDomain *out, int maxCount);
void dnsMonitor_unknownClear();

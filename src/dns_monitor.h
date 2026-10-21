#pragma once
#include <Arduino.h>
#include <IPAddress.h>

struct DnsLogEntry {
    IPAddress clientIp;
    uint32_t  timestamp;     // millis()
    char      domain[96];
};

void dnsMonitor_init();      // starts a background task; loop() needs nothing

// Copies the log, newest first. Returns the number of entries copied.
int  dnsMonitor_snapshot(DnsLogEntry *out, int maxCount);

#pragma once
#include <Arduino.h>

struct DnsLogEntry {
    IPAddress clientIp;
    char      domain[96];
    uint32_t  timestamp;
};

void dnsMonitor_init();
void dnsMonitor_loop();

// Returns pointer to the ring buffer of recent lookups
DnsLogEntry* dnsMonitor_getLog(int &count);

#pragma once
#include <Arduino.h>
#include <IPAddress.h>

struct DnsLogEntry {
    IPAddress clientIp;
    uint32_t  timestamp;     // millis()
    uint8_t   app;           // APP_* id, APP_OTHER when not recognised
    uint8_t   blocked;       // 0 = let through, 1 = your block list, 2 = built-in ad/tracker list
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

// ---- most used domains per client (today, by number of lookups) ----
struct TopDomain { char domain[40]; uint16_t count; uint8_t app; };
int  dnsMonitor_top(uint32_t clientIp, TopDomain *out, int maxCount);     // most asked first

// ---- which DNS server the repeater asks ----
struct DnsPreset { const char *name; const char *a; const char *b; };      // a/b = IPv4 text, "" = none
const DnsPreset *dnsMonitor_presets(int &count);                            // index 0 = the router's own
uint8_t dnsMonitor_preset();                                                // chosen index (last = custom)
void    dnsMonitor_servers(IPAddress &a, IPAddress &b);                     // what is used right now
void    dnsMonitor_custom(IPAddress &a, IPAddress &b);
bool    dnsMonitor_setUpstream(uint8_t preset, const char *ipA, const char *ipB, String &err);

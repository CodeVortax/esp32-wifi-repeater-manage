#include "dns_monitor.h"
#include "config.h"
#include "wifi_repeater.h"
#include "apps.h"
#include "dns_parse.h"
#include "adblock.h"
#include "quota_manager.h"
#include <Preferences.h>
#include <time.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#define DNS_PORT 53

static DnsLogEntry   logRing[DNS_LOG_SIZE];
static int           logHead  = 0;      // next slot to write
static int           logCount = 0;
static portMUX_TYPE  logMux   = portMUX_INITIALIZER_UNLOCKED;

// ---- names nobody claimed ----------------------------------------------------

static UnknownDomain unknownTab[UNKNOWN_DOMAIN_MAX];
static int           unknownN = 0;

static bool ignorable(const char *d) {
    size_t n = strlen(d);
    auto ends = [&](const char *suf) { size_t l = strlen(suf); return n >= l && strcmp(d + n - l, suf) == 0; };
    return ends(".arpa") || ends(".local") || ends(".lan") || ends(".home") || ends(".localdomain") || strchr(d, '.') == nullptr;
}

static void noteUnknown(IPAddress ip, const char *domain) {
    if (ignorable(domain)) return;
    portENTER_CRITICAL(&logMux);
    int idx = -1;
    for (int i = 0; i < unknownN; i++)
        if (strncmp(unknownTab[i].domain, domain, sizeof(unknownTab[i].domain) - 1) == 0) { idx = i; break; }
    if (idx < 0) {
        if (unknownN < UNKNOWN_DOMAIN_MAX) idx = unknownN++;
        else {                                            // full: replace the least asked one
            idx = 0;
            for (int i = 1; i < unknownN; i++) if (unknownTab[i].count < unknownTab[idx].count) idx = i;
        }
        memset(&unknownTab[idx], 0, sizeof(UnknownDomain));
        strncpy(unknownTab[idx].domain, domain, sizeof(unknownTab[idx].domain) - 1);
    }
    unknownTab[idx].count++;
    unknownTab[idx].clientIp = (uint32_t)ip;
    portEXIT_CRITICAL(&logMux);
}

int dnsMonitor_unknownSnapshot(UnknownDomain *out, int maxCount) {
    int n;
    portENTER_CRITICAL(&logMux);
    n = unknownN < maxCount ? unknownN : maxCount;
    memcpy(out, unknownTab, n * sizeof(UnknownDomain));
    portEXIT_CRITICAL(&logMux);
    for (int i = 1; i < n; i++) {                         // insertion sort, most asked first (n <= 40)
        UnknownDomain k = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].count < k.count) { out[j + 1] = out[j]; j--; }
        out[j + 1] = k;
    }
    return n;
}

void dnsMonitor_unknownClear() {
    portENTER_CRITICAL(&logMux);
    unknownN = 0;
    portEXIT_CRITICAL(&logMux);
}

static void logQuery(IPAddress ip, const char *domain, uint8_t app, uint8_t blocked) {
    portENTER_CRITICAL(&logMux);
    DnsLogEntry &e = logRing[logHead];
    e.clientIp  = ip;
    e.timestamp = millis();
    e.app       = app;
    e.blocked   = blocked;
    strncpy(e.domain, domain, sizeof(e.domain) - 1);
    e.domain[sizeof(e.domain) - 1] = '\0';
    logHead = (logHead + 1) % DNS_LOG_SIZE;
    if (logCount < DNS_LOG_SIZE) logCount++;
    portEXIT_CRITICAL(&logMux);
}

int dnsMonitor_snapshot(DnsLogEntry *out, int maxCount) {
    int n = 0;
    portENTER_CRITICAL(&logMux);
    for (int k = 0; k < logCount && n < maxCount; k++) {
        int idx = (logHead - 1 - k + DNS_LOG_SIZE * 2) % DNS_LOG_SIZE;   // newest first
        out[n++] = logRing[idx];
    }
    portEXIT_CRITICAL(&logMux);
    return n;
}

// ---- most used domains ----------------------------------------------------------------------
#define TOP_CLIENTS 16
#define TOP_PER_CLIENT 12
struct TopRow { uint32_t ip; TopDomain d[TOP_PER_CLIENT]; };
static TopRow topRows[TOP_CLIENTS];
static int    topDay = -1;

static void noteTop(IPAddress ip, const char *domain, uint8_t app) {
    if (ignorable(domain)) return;
    char base[40];
    dns_baseDomain(domain, base, sizeof(base));
    uint32_t key = (uint32_t)ip;
    time_t now = time(nullptr);
    int day = -1;
    if (now > 1700000000) { struct tm t; localtime_r(&now, &t); day = t.tm_yday; }
    portENTER_CRITICAL(&logMux);
    if (day >= 0 && day != topDay) { memset(topRows, 0, sizeof(topRows)); topDay = day; }   // a new day starts empty
    TopRow *row = nullptr, *free_ = nullptr;
    for (int i = 0; i < TOP_CLIENTS; i++) {
        if (topRows[i].ip == key) { row = &topRows[i]; break; }
        if (topRows[i].ip == 0 && !free_) free_ = &topRows[i];
    }
    if (!row) row = free_ ? free_ : &topRows[key % TOP_CLIENTS];
    if (row->ip != key) { memset(row, 0, sizeof(*row)); row->ip = key; }
    int idx = -1;
    for (int i = 0; i < TOP_PER_CLIENT; i++) if (row->d[i].count && strcmp(row->d[i].domain, base) == 0) { idx = i; break; }
    if (idx < 0) {
        idx = 0;
        for (int i = 0; i < TOP_PER_CLIENT; i++) {
            if (row->d[i].count == 0) { idx = i; break; }
            if (row->d[i].count < row->d[idx].count) idx = i;
        }
        memset(&row->d[idx], 0, sizeof(TopDomain));
        strncpy(row->d[idx].domain, base, sizeof(row->d[idx].domain) - 1);
    }
    if (row->d[idx].count < 65535) row->d[idx].count++;
    row->d[idx].app = app;
    portEXIT_CRITICAL(&logMux);
}

int dnsMonitor_top(uint32_t clientIp, TopDomain *out, int maxCount) {
    int n = 0;
    portENTER_CRITICAL(&logMux);
    for (int i = 0; i < TOP_CLIENTS && clientIp; i++) {
        if (topRows[i].ip != clientIp) continue;
        for (int k = 0; k < TOP_PER_CLIENT && n < maxCount; k++) if (topRows[i].d[k].count) out[n++] = topRows[i].d[k];
        break;
    }
    portEXIT_CRITICAL(&logMux);
    for (int i = 1; i < n; i++) {
        TopDomain k = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].count < k.count) { out[j + 1] = out[j]; j--; }
        out[j + 1] = k;
    }
    return n;
}

// ---- upstream DNS ---------------------------------------------------------------------------
// Plain DNS over UDP (port 53). The "family" resolvers answer adult / malware names with a block
// page address or nothing, which is how they filter. DNS over HTTPS / TLS is not supported.
static const DnsPreset PRESETS[] = {
    { "Router (automatic)",                       "", "" },
    { "Cloudflare 1.1.1.1 (fast)",                "1.1.1.1", "1.0.0.1" },
    { "Cloudflare Security (blocks malware)",     "1.1.1.2", "1.0.0.2" },
    { "Cloudflare Family (malware + adult)",      "1.1.1.3", "1.0.0.3" },
    { "Google 8.8.8.8",                           "8.8.8.8", "8.8.4.4" },
    { "Quad9 (blocks malware)",                   "9.9.9.9", "149.112.112.112" },
    { "AdGuard (ads + trackers)",                 "94.140.14.14", "94.140.15.15" },
    { "AdGuard Family (ads + adult)",             "94.140.14.15", "94.140.15.16" },
    { "OpenDNS FamilyShield (adult)",             "208.67.222.123", "208.67.220.123" },
    { "CleanBrowsing Family (adult)",             "185.228.168.168", "185.228.169.168" },
    { "Custom",                                   "", "" },
};
static const int PRESET_COUNT = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
static volatile uint8_t  presetSel = 0;
static volatile uint32_t customA = 0, customB = 0;          // IPv4 header byte order

const DnsPreset *dnsMonitor_presets(int &count) { count = PRESET_COUNT; return PRESETS; }
uint8_t dnsMonitor_preset() { return presetSel; }
void dnsMonitor_custom(IPAddress &a, IPAddress &b) { a = IPAddress(customA); b = IPAddress(customB); }

static void loadUpstreamSettings() {
    Preferences p;
    p.begin("dnsup", false);
    uint8_t v = p.getUChar("p", 0);
    presetSel = v < PRESET_COUNT ? v : 0;
    customA = p.getULong("a", 0);
    customB = p.getULong("b", 0);
    p.end();
}

bool dnsMonitor_setUpstream(uint8_t preset, const char *ipA, const char *ipB, String &err) {
    if (preset >= PRESET_COUNT) { err = "Unknown DNS choice."; return false; }
    IPAddress a, b;
    if (preset == PRESET_COUNT - 1) {                           // custom: validate
        if (!a.fromString(ipA) || a == IPAddress(0, 0, 0, 0)) { err = "The first DNS address is not valid."; return false; }
        if (ipB && ipB[0] && (!b.fromString(ipB))) { err = "The second DNS address is not valid."; return false; }
        if (a == WiFi.softAPIP() || b == WiFi.softAPIP()) { err = "That is the repeater itself."; return false; }
        customA = (uint32_t)a;
        customB = (uint32_t)b;
    }
    presetSel = preset;
    Preferences p;
    p.begin("dnsup", false);
    p.putUChar("p", preset);
    if (preset == PRESET_COUNT - 1) { p.putULong("a", customA); p.putULong("b", customB); }
    p.end();
    return true;
}

static IPAddress upstreamDns();

// The servers to ask, in order. Router mode: the router's DNS, then a public one as a spare.
void dnsMonitor_servers(IPAddress &a, IPAddress &b) {
    uint8_t sel = presetSel;
    a = IPAddress(0, 0, 0, 0);
    b = IPAddress(0, 0, 0, 0);
    if (sel == 0) {
        a = upstreamDns();
        IPAddress spare; spare.fromString(DNS_FALLBACK_UPSTREAM);
        if (a != spare) b = spare;
    } else if (sel == PRESET_COUNT - 1) {
        a = IPAddress(customA); b = IPAddress(customB);
    } else {
        a.fromString(PRESETS[sel].a);
        b.fromString(PRESETS[sel].b);
    }
    if (a == WiFi.softAPIP()) a = IPAddress(0, 0, 0, 0);          // never ask ourselves: that loops
    if (b == WiFi.softAPIP()) b = IPAddress(0, 0, 0, 0);
}

static IPAddress upstreamDns() {
    IPAddress ip = wifiRepeater_getUpstreamDns();
    if (ip == IPAddress(0, 0, 0, 0)) ip.fromString(DNS_FALLBACK_UPSTREAM);
    // Never forward to ourselves: that would loop forever.
    if (ip == WiFi.softAPIP()) ip.fromString(DNS_FALLBACK_UPSTREAM);
    return ip;
}

// Transparent proxy: log the query, forward it upstream, relay the answer.
// Runs in its own task so a slow upstream never stalls the rest of the firmware.
static void dnsTask(void *) {
    static uint8_t query[512];
    static uint8_t reply[1232];         // enough for EDNS0 answers
    WiFiUDP server;
    server.begin(DNS_PORT);
    Serial.println("[DNS] monitor/proxy listening on :53");

    for (;;) {
        int size = server.parsePacket();
        if (size <= 0) { vTaskDelay(pdMS_TO_TICKS(5)); continue; }
        if (size > (int)sizeof(query)) {            // oversized query: drain and ignore
            while (server.available()) server.read();
            continue;
        }

        IPAddress clientIp   = server.remoteIP();
        uint16_t  clientPort = server.remotePort();
        int len = server.read(query, size);
        if (len < 12) continue;

        char domain[96];
        uint8_t qApp = APP_OTHER;
        if (dns_parseQName(query, len, domain, sizeof(domain))) {
            // The repeater's own pages: "usage.lan" / "my.usage" are always the repeater, and a client that is
            // paused (limit reached, blocked, internet off) or has an unread 80% notice is sent to the
            // notice page: every name for a paused client, only the "is there a login page?" names for a notice.
            {
                bool mine = strcmp(domain, "usage.lan") == 0 || strcmp(domain, "my.usage") == 0;
                bool notice = !mine && quota_wantsNotice((uint32_t)clientIp, dns_isConnectivityCheck(domain));
                if (mine || notice) {
                    IPAddress ap = WiFi.softAPIP();
                    uint8_t apIp[4] = { ap[0], ap[1], ap[2], ap[3] };
                    int bl = dns_buildAnswerA(query, len, reply, sizeof(reply), apIp, mine ? 60 : 5);
                    if (bl > 0) {
                        server.beginPacket(clientIp, clientPort);
                        server.write(reply, bl);
                        server.endPacket();
                        continue;
                    }
                }
            }
            // "Internet off": nobody except trusted devices gets an answer that leads anywhere.
            if (quota_internetOff() && !quota_isTrustedIp((uint32_t)clientIp)) {
                int bl = dns_buildBlocked(query, len, reply, sizeof(reply));
                if (bl > 0) { server.beginPacket(clientIp, clientPort); server.write(reply, bl); server.endPacket(); }
                continue;
            }
            // Ads, trackers and the sites you blocked: answer here, forward nothing.
            AdVerdict verdict = ignorable(domain) ? AB_PASS : adblock_check(domain);
            if (verdict != AB_PASS) {
                int bl = dns_buildBlocked(query, len, reply, sizeof(reply));
                if (bl > 0) {
                    logQuery(clientIp, domain, APP_OTHER, (uint8_t)verdict);
                    adblock_noteBlocked((uint32_t)clientIp, domain, verdict);
                    server.beginPacket(clientIp, clientPort);
                    server.write(reply, bl);
                    server.endPacket();
                    continue;
                }
            }
            qApp = apps_classifyDomain(domain);
            if (qApp != APP_OTHER && quota_ipAppBlocked((uint32_t)clientIp, qApp)) {      // an app killed for this user
                int bl = dns_buildBlocked(query, len, reply, sizeof(reply));
                if (bl > 0) {
                    logQuery(clientIp, domain, qApp, (uint8_t)AB_APP);
                    server.beginPacket(clientIp, clientPort);
                    server.write(reply, bl);
                    server.endPacket();
                    continue;
                }
            }
            logQuery(clientIp, domain, qApp, 0);
            noteTop(clientIp, domain, qApp);
        } else {
            domain[0] = '\0';
        }

        if (!wifiRepeater_isUpstreamConnected()) continue;   // nothing to forward to

        // Ask the first server; if it stays silent, the second one.
        IPAddress servers[2];
        dnsMonitor_servers(servers[0], servers[1]);
        int respLen = 0;
        for (int k = 0; k < 2 && respLen == 0; k++) {
            if (servers[k] == IPAddress(0, 0, 0, 0)) continue;
            WiFiUDP upstream;
            if (!upstream.begin(0)) continue;
            upstream.beginPacket(servers[k], DNS_PORT);
            upstream.write(query, len);
            upstream.endPacket();

            // Wait for the answer that carries OUR transaction id (first 2 bytes).
            uint32_t start = millis();
            uint32_t waitMs = (k == 0 && servers[1] != IPAddress(0, 0, 0, 0)) ? 1200 : 1800;
            while (millis() - start < waitMs) {
                int rs = upstream.parsePacket();
                if (rs > 0) {
                    int got = upstream.read(reply, rs > (int)sizeof(reply) ? sizeof(reply) : rs);
                    if (got >= 12 && reply[0] == query[0] && reply[1] == query[1]) { respLen = got; break; }
                } else {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
            }
            upstream.stop();
        }

        if (respLen > 0) {
            // Remember which app these addresses belong to, so the bytes that follow can be counted for it.
            DnsLearn learn;
            if (dns_learnFromReply(reply, respLen, qApp, learn)) {
                uint16_t nowMin = (uint16_t)(millis() / 60000UL);
                for (int k = 0; k < learn.nIps; k++) apps_cachePut(learn.ips[k], learn.app, nowMin);
                if (learn.app == APP_OTHER && domain[0] && learn.nIps > 0) noteUnknown(clientIp, domain);
            }
            server.beginPacket(clientIp, clientPort);
            server.write(reply, respLen);
            server.endPacket();
        }
    }
}

void dnsMonitor_init() {
    loadUpstreamSettings();
    memset(topRows, 0, sizeof(topRows));
    memset(logRing, 0, sizeof(logRing));
    memset(unknownTab, 0, sizeof(unknownTab));
    xTaskCreate(dnsTask, "dns", 8192, nullptr, 1, nullptr);
}

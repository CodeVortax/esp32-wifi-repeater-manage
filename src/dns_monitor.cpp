#include "dns_monitor.h"
#include "config.h"
#include "wifi_repeater.h"
#include "apps.h"
#include "dns_parse.h"
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

static void logQuery(IPAddress ip, const char *domain, uint8_t app) {
    portENTER_CRITICAL(&logMux);
    DnsLogEntry &e = logRing[logHead];
    e.clientIp  = ip;
    e.timestamp = millis();
    e.app       = app;
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
            qApp = apps_classifyDomain(domain);
            logQuery(clientIp, domain, qApp);
        } else {
            domain[0] = '\0';
        }

        if (!wifiRepeater_isUpstreamConnected()) continue;   // nothing to forward to

        WiFiUDP upstream;
        if (!upstream.begin(0)) continue;
        upstream.beginPacket(upstreamDns(), DNS_PORT);
        upstream.write(query, len);
        upstream.endPacket();

        // Wait for the answer that carries OUR transaction id (first 2 bytes).
        uint32_t start = millis();
        int respLen = 0;
        while (millis() - start < 1500) {
            int rs = upstream.parsePacket();
            if (rs > 0) {
                int got = upstream.read(reply, rs > (int)sizeof(reply) ? sizeof(reply) : rs);
                if (got >= 12 && reply[0] == query[0] && reply[1] == query[1]) { respLen = got; break; }
            } else {
                vTaskDelay(pdMS_TO_TICKS(2));
            }
        }
        upstream.stop();

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
    memset(logRing, 0, sizeof(logRing));
    memset(unknownTab, 0, sizeof(unknownTab));
    xTaskCreate(dnsTask, "dns", 8192, nullptr, 1, nullptr);
}

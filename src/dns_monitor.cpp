#include "dns_monitor.h"
#include "config.h"
#include "wifi_repeater.h"
#include <WiFi.h>
#include <WiFiUdp.h>

#define DNS_PORT 53

static DnsLogEntry   logRing[DNS_LOG_SIZE];
static int           logHead  = 0;      // next slot to write
static int           logCount = 0;
static portMUX_TYPE  logMux   = portMUX_INITIALIZER_UNLOCKED;

// Minimal DNS QNAME parser: pulls the queried hostname out of a raw DNS query.
static bool parseQName(const uint8_t *buf, int len, char *out, int outSize) {
    if (len < 13) return false;
    int pos = 12;                       // skip the header
    int o = 0;
    while (pos < len && buf[pos] != 0) {
        int labelLen = buf[pos++];
        if ((labelLen & 0xC0) != 0) break;              // compression pointer: not valid in a query
        if (pos + labelLen > len) break;
        if (o + labelLen + 2 >= outSize) break;
        if (o > 0) out[o++] = '.';
        for (int i = 0; i < labelLen; i++) {            // keep the log (and the web UI) printable
            uint8_t c = buf[pos + i];
            out[o++] = (c >= 0x21 && c < 0x7F) ? (char)c : '?';
        }
        pos += labelLen;
    }
    out[o] = '\0';
    return o > 0;
}

static void logQuery(IPAddress ip, const char *domain) {
    portENTER_CRITICAL(&logMux);
    DnsLogEntry &e = logRing[logHead];
    e.clientIp  = ip;
    e.timestamp = millis();
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
        if (parseQName(query, len, domain, sizeof(domain))) logQuery(clientIp, domain);

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
            server.beginPacket(clientIp, clientPort);
            server.write(reply, respLen);
            server.endPacket();
        }
    }
}

void dnsMonitor_init() {
    memset(logRing, 0, sizeof(logRing));
    xTaskCreate(dnsTask, "dns", 6144, nullptr, 1, nullptr);
}

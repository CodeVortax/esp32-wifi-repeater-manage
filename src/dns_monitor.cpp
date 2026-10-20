#include "dns_monitor.h"
#include <WiFiUdp.h>

#define DNS_PORT        53
#define UPSTREAM_DNS_IP IPAddress(1, 1, 1, 1)
#define LOG_SIZE        40

static WiFiUDP udp;
static DnsLogEntry log_[LOG_SIZE];
static int logHead = 0;
static uint8_t packet[512];

// Minimal DNS QNAME parser: pulls the queried hostname out of a raw
// DNS query packet (labels prefixed by length byte, ending in 0x00).
static bool parseQName(uint8_t *buf, int len, char *out, int outSize) {
    if (len < 12) return false;
    int pos = 12; // skip header
    int o = 0;
    while (pos < len && buf[pos] != 0) {
        int labelLen = buf[pos++];
        if (labelLen == 0 || pos + labelLen > len) break;
        if (o + labelLen + 1 >= outSize) break;
        if (o > 0) out[o++] = '.';
        memcpy(out + o, buf + pos, labelLen);
        o += labelLen;
        pos += labelLen;
    }
    out[o] = '\0';
    return o > 0;
}

void dnsMonitor_init() {
    udp.begin(DNS_PORT);
    memset(log_, 0, sizeof(log_));
    Serial.println("[DNS] monitor/proxy listening on :53");
}

void dnsMonitor_loop() {
    int packetSize = udp.parsePacket();
    if (packetSize <= 0) return;
    if (packetSize > (int)sizeof(packet)) packetSize = sizeof(packet);

    IPAddress clientIp = udp.remoteIP();
    uint16_t clientPort = udp.remotePort();
    udp.read(packet, packetSize);

    char domain[96];
    if (parseQName(packet, packetSize, domain, sizeof(domain))) {
        DnsLogEntry &entry = log_[logHead];
        entry.clientIp   = clientIp;
        entry.timestamp  = millis();
        strncpy(entry.domain, domain, sizeof(entry.domain) - 1);
        logHead = (logHead + 1) % LOG_SIZE;
        Serial.printf("[DNS] %s -> %s\n", clientIp.toString().c_str(), domain);
    }

    // Forward the original query upstream and relay the answer back —
    // keeps this a transparent proxy rather than breaking resolution.
    WiFiUDP upstream;
    upstream.begin(0);
    upstream.beginPacket(UPSTREAM_DNS_IP, DNS_PORT);
    upstream.write(packet, packetSize);
    upstream.endPacket();

    uint32_t start = millis();
    int respLen = 0;
    while (millis() - start < 300) {
        respLen = upstream.parsePacket();
        if (respLen > 0) break;
    }
    if (respLen > 0) {
        uint8_t resp[512];
        if (respLen > (int)sizeof(resp)) respLen = sizeof(resp);
        upstream.read(resp, respLen);
        udp.beginPacket(clientIp, clientPort);
        udp.write(resp, respLen);
        udp.endPacket();
    }
    upstream.stop();
}

DnsLogEntry* dnsMonitor_getLog(int &count) {
    count = LOG_SIZE;
    return log_;
}

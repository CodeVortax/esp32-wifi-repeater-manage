#include "dns_parse.h"
#include "apps.h"
#include <string.h>

bool dns_parseQName(const uint8_t *buf, int len, char *out, int outSize) {
    if (len < 13 || outSize < 4) return false;
    int pos = 12;                                   // skip the header
    int o = 0;
    while (pos < len && buf[pos] != 0) {
        int labelLen = buf[pos++];
        if ((labelLen & 0xC0) != 0) break;          // compression pointer: not valid in a query
        if (pos + labelLen > len) break;
        if (o + labelLen + 2 >= outSize) break;
        if (o > 0) out[o++] = '.';
        for (int i = 0; i < labelLen; i++) {        // keep the log (and the web UI) printable
            uint8_t c = buf[pos + i];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[o++] = (c >= 0x21 && c < 0x7F) ? (char)c : '?';
        }
        pos += labelLen;
    }
    out[o] = '\0';
    return o > 0;
}

// Position just after the name that starts at `pos`, or -1.
static int skipName(const uint8_t *b, int len, int pos) {
    int guard = 0;
    while (pos < len && guard++ < 128) {
        uint8_t c = b[pos];
        if (c == 0) return pos + 1;
        if ((c & 0xC0) == 0xC0) return (pos + 2 <= len) ? pos + 2 : -1;
        if ((c & 0xC0) != 0) return -1;
        pos += 1 + c;
    }
    return -1;
}

// Reads a (possibly compressed) name as dotted lower case. Returns its length or -1.
static int readName(const uint8_t *b, int len, int pos, char *out, int outSize) {
    int o = 0, hops = 0;
    while (pos < len) {
        uint8_t c = b[pos];
        if (c == 0) { out[o] = '\0'; return o; }
        if ((c & 0xC0) == 0xC0) {
            if (pos + 2 > len || ++hops > 8) return -1;
            pos = ((c & 0x3F) << 8) | b[pos + 1];
            continue;
        }
        if ((c & 0xC0) != 0) return -1;
        pos++;
        if (pos + c > len || o + c + 2 >= outSize) return -1;
        if (o > 0) out[o++] = '.';
        for (int i = 0; i < c; i++) {
            uint8_t ch = b[pos + i];
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            out[o++] = (char)ch;
        }
        pos += c;
    }
    return -1;
}

bool dns_learnFromReply(const uint8_t *r, int len, uint8_t questionApp, DnsLearn &out) {
    out.app = questionApp;
    out.nIps = 0;
    if (len < 12) return false;
    if (!(r[2] & 0x80)) return false;                         // not a response
    if ((r[3] & 0x0F) != 0) return false;                     // error reply
    int qd = (r[4] << 8) | r[5];
    int an = (r[6] << 8) | r[7];
    int pos = 12;
    for (int i = 0; i < qd; i++) {
        pos = skipName(r, len, pos);
        if (pos < 0 || pos + 4 > len) return false;
        pos += 4;
    }
    if (an > 32) an = 32;
    for (int i = 0; i < an; i++) {
        int rdPos = skipName(r, len, pos);
        if (rdPos < 0 || rdPos + 10 > len) return false;
        int type  = (r[rdPos] << 8) | r[rdPos + 1];
        int rdLen = (r[rdPos + 8] << 8) | r[rdPos + 9];
        int data  = rdPos + 10;
        if (data + rdLen > len) return false;
        if (type == 5 && out.app == APP_OTHER) {              // CNAME: a CDN name often tells the app
            char cname[96];
            if (readName(r, len, data, cname, sizeof(cname)) > 0) out.app = apps_classifyDomain(cname);
        } else if (type == 1 && rdLen == 4 && out.nIps < 8) { // A record
            uint32_t ip;
            memcpy(&ip, r + data, 4);
            out.ips[out.nIps++] = ip;
        }
        pos = data + rdLen;
    }
    return true;
}

int dns_buildAnswerA(const uint8_t *q, int len, uint8_t *out, int cap, const uint8_t ip[4], uint32_t ttl) {
    if (len < 17 || cap < len + 16) return 0;
    if ((q[2] & 0x80) != 0) return 0;                        // a response, not a query
    if (((q[2] >> 3) & 0x0F) != 0) return 0;                 // only standard queries
    if (q[4] != 0 || q[5] != 1) return 0;                    // exactly one question
    int end = skipName(q, len, 12);
    if (end < 0 || end + 4 > len) return 0;
    if (q[12] & 0xC0) return 0;
    int qEnd = end + 4;                                      // type + class
    uint16_t qtype = (uint16_t)((q[end] << 8) | q[end + 1]);

    memcpy(out, q, qEnd);
    out[2] = (uint8_t)(0x80 | (q[2] & 0x01));                // QR, RD copied
    out[3] = 0x80;                                           // RA, NOERROR
    out[6] = 0; out[7] = (qtype == 1) ? 1 : 0;               // answers
    out[8] = out[9] = out[10] = out[11] = 0;                 // no authority / extra
    int o = qEnd;
    if (qtype == 1) {
        uint8_t ans[16] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0x00, 0x04, 0, 0, 0, 0 };
        ans[6] = (uint8_t)(ttl >> 24); ans[7] = (uint8_t)(ttl >> 16); ans[8] = (uint8_t)(ttl >> 8); ans[9] = (uint8_t)ttl;
        memcpy(ans + 12, ip, 4);
        memcpy(out + o, ans, sizeof(ans));
        o += sizeof(ans);
    }
    return o;
}

void dns_baseDomain(const char *name, char *out, int outSize) {
    int n = (int)strlen(name);
    int dots[8], nd = 0;
    for (int i = 0; i < n && nd < 8; i++) if (name[i] == '.') dots[nd++] = i;
    int start = 0;                                     // where the kept part begins
    if (nd >= 2) {
        int tldLen = n - dots[nd - 1] - 1;
        int secLen = dots[nd - 1] - dots[nd - 2] - 1;
        const char *sec = name + dots[nd - 2] + 1;
        bool twoPart = tldLen == 2 && secLen <= 3 &&
            ((secLen == 2 && (!strncmp(sec, "co", 2) || !strncmp(sec, "ac", 2) || !strncmp(sec, "or", 2) || !strncmp(sec, "ne", 2) || !strncmp(sec, "go", 2))) ||
             (secLen == 3 && (!strncmp(sec, "com", 3) || !strncmp(sec, "net", 3) || !strncmp(sec, "org", 3) || !strncmp(sec, "gov", 3) || !strncmp(sec, "edu", 3))));
        start = (twoPart && nd >= 3) ? dots[nd - 3] + 1 : dots[nd - 2] + 1;
    }
    int l = n - start;
    if (l >= outSize) l = outSize - 1;
    memcpy(out, name + start, l);
    out[l] = '\0';
}

int dns_buildBlocked(const uint8_t *q, int len, uint8_t *out, int cap) {
    static const uint8_t zero[4] = { 0, 0, 0, 0 };
    return dns_buildAnswerA(q, len, out, cap, zero, 300);
}

bool dns_isConnectivityCheck(const char *name) {
    static const char *const list[] = {
        "connectivitycheck.gstatic.com", "connectivitycheck.android.com", "clients3.google.com", "clients1.google.com",
        "captive.apple.com", "msftconnecttest.com", "msftncsi.com", "detectportal.firefox.com", "nmcheck.gnome.org",
    };
    size_t n = strlen(name);
    for (size_t i = 0; i < sizeof(list) / sizeof(list[0]); i++) {
        size_t l = strlen(list[i]);
        if (n >= l && strcmp(name + n - l, list[i]) == 0 && (n == l || name[n - l - 1] == '.')) return true;
    }
    return false;
}

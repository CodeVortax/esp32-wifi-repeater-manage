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

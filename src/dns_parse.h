#pragma once
// DNS packet helpers. Pure C++ (no Arduino headers), unit-tested on a PC: make -C test
#include <stdint.h>

// Pulls the queried host name (lower case, printable) out of a raw DNS query.
bool dns_parseQName(const uint8_t *buf, int len, char *out, int outSize);

struct DnsLearn {
    uint8_t  app;          // app the answer belongs to (question name first, then CNAME targets)
    int      nIps;
    uint32_t ips[8];       // IPv4 answers, in the byte order an IPv4 header is loaded in
};

// Reads a DNS reply. questionApp is the app already found for the question name.
// Returns false when the packet is malformed or has no usable answer section.
bool dns_learnFromReply(const uint8_t *reply, int len, uint8_t questionApp, DnsLearn &out);

// "rr3---sn.googlevideo.com" -> "googlevideo.com", "a.b.example.co.uk" -> "example.co.uk".
void dns_baseDomain(const char *name, char *out, int outSize);

// Answer for a blocked name, built from the query itself: type A gets 0.0.0.0 (TTL 300), every
// other type gets "no data". Returns the reply length, or 0 when the query is not a plain
// single-question query (the caller then forwards it normally).
int dns_buildBlocked(const uint8_t *query, int len, uint8_t *out, int cap);

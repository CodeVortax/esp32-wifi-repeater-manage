// Host tests: apps (domain matching, IP memory), dns_parse, shaper.
#include "apps.h"
#include "dns_parse.h"
#include "shaper.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_APP(domain, expect) do { uint8_t got = apps_classifyDomain(domain); checks++; \
    if (got != (expect)) { failures++; printf("FAIL %s:%d  %s -> %s (wanted %s)\n", __FILE__, __LINE__, domain, apps_name(got), apps_name(expect)); } } while (0)

static uint32_t ip4(int a, int b, int c, int d) { return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24); }

// ---- tiny DNS reply builder ----
static void putName(std::vector<uint8_t> &p, const std::string &name) {
    size_t i = 0;
    while (i < name.size()) {
        size_t dot = name.find('.', i);
        if (dot == std::string::npos) dot = name.size();
        p.push_back((uint8_t)(dot - i));
        for (size_t k = i; k < dot; k++) p.push_back((uint8_t)name[k]);
        i = dot + 1;
    }
    p.push_back(0);
}
static void put16(std::vector<uint8_t> &p, int v) { p.push_back(v >> 8); p.push_back(v & 0xFF); }
static void put32(std::vector<uint8_t> &p, uint32_t v) { for (int i = 3; i >= 0; i--) p.push_back((v >> (8 * i)) & 0xFF); }

static std::vector<uint8_t> makeQuery(const std::string &name) {
    std::vector<uint8_t> p = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    putName(p, name); put16(p, 1); put16(p, 1);
    return p;
}
// reply: question, optional CNAME chain (compressed pointer to the question for the first name), then A records
static std::vector<uint8_t> makeReply(const std::string &q, const std::vector<std::string> &cnames, const std::vector<uint32_t> &ips) {
    std::vector<uint8_t> p = {0x12, 0x34, 0x81, 0x80, 0, 1};
    put16(p, (int)(cnames.size() + ips.size())); put16(p, 0); put16(p, 0);
    putName(p, q); put16(p, 1); put16(p, 1);
    for (const auto &c : cnames) {
        p.push_back(0xC0); p.push_back(12);               // owner: pointer to the question name
        put16(p, 5); put16(p, 1); put32(p, 60);
        std::vector<uint8_t> rd; putName(rd, c);
        put16(p, (int)rd.size()); p.insert(p.end(), rd.begin(), rd.end());
    }
    for (uint32_t ip : ips) {
        p.push_back(0xC0); p.push_back(12);
        put16(p, 1); put16(p, 1); put32(p, 60); put16(p, 4);
        for (int i = 0; i < 4; i++) p.push_back((ip >> (8 * i)) & 0xFF);   // keep the in-memory byte order
    }
    return p;
}

static void testClassify() {
    CHECK_APP("www.youtube.com", APP_YOUTUBE);
    CHECK_APP("rr3---sn-p5qlsn7e.googlevideo.com", APP_YOUTUBE);
    CHECK_APP("music.youtube.com", APP_YTMUSIC);               // longer suffix wins
    CHECK_APP("i.ytimg.com", APP_YOUTUBE);
    CHECK_APP("v16-webapp.tiktokcdn.com", APP_TIKTOK);
    CHECK_APP("api16-normal-c-alisg.tiktokv.com", APP_TIKTOK);
    CHECK_APP("mmg.whatsapp.net", APP_WHATSAPP);
    CHECK_APP("e1.whatsapp.net", APP_WHATSAPP);
    CHECK_APP("scontent-cai1-1.xx.fbcdn.net", APP_FACEBOOK);
    CHECK_APP("scontent.cdninstagram.com", APP_INSTAGRAM);
    CHECK_APP("app.snapchat.com", APP_SNAPCHAT);
    CHECK_APP("cf-st.sc-cdn.net", APP_SNAPCHAT);
    CHECK_APP("web.telegram.org", APP_TELEGRAM);
    CHECK_APP("occ-0-1234.nflxvideo.net", APP_NETFLIX);
    CHECK_APP("i.scdn.co", APP_SPOTIFY);
    CHECK_APP("meet.google.com", APP_MEET);
    CHECK_APP("gemini.google.com", APP_AI);
    CHECK_APP("www.google.com", APP_GOOGLE);
    CHECK_APP("play.googleapis.com", APP_PLAYSTORE);
    CHECK_APP("edgedl.me.gvt1.com", APP_PLAYSTORE);
    CHECK_APP("tlu.dl.delivery.mp.microsoft.com", APP_WINUPDATE);
    CHECK_APP("t.co", APP_XTWITTER);
    CHECK_APP("WWW.YouTube.COM.", APP_YOUTUBE);                // case and trailing dot
    // The match must stop at a label boundary.
    CHECK_APP("notyoutube.com", APP_OTHER);
    CHECK_APP("fakefb.com", APP_OTHER);
    CHECK_APP("dt.co", APP_OTHER);
    CHECK_APP("example.org", APP_OTHER);
    CHECK_APP("", APP_OTHER);
    CHECK(apps_category(APP_YOUTUBE) == CAT_VIDEO);
    CHECK(apps_category(APP_WHATSAPP) == CAT_MESSAGING);
    CHECK(apps_category(APP_STEAM) == CAT_GAMING);
    CHECK(apps_category(APP_WINUPDATE) == CAT_DOWNLOADS);
    CHECK(apps_isGeneric(APP_GOOGLE) && !apps_isGeneric(APP_YOUTUBE));
    for (int a = 0; a < APP_COUNT; a++) CHECK(apps_name(a) && apps_name(a)[0] && apps_category(a) < CAT_COUNT);
}

static void testCustomRules() {
    CHECK(apps_validSuffix("example.com") && apps_validSuffix("a-b.co.uk"));
    CHECK(!apps_validSuffix("nodot") && !apps_validSuffix("Upper.com") && !apps_validSuffix(".lead.com") &&
          !apps_validSuffix("trail.") && !apps_validSuffix("dou..ble.com") && !apps_validSuffix("sp ace.com") && !apps_validSuffix("ab"));
    CHECK_APP("cdn.example-games.com", APP_OTHER);
    CHECK(apps_customAdd("example-games.com", APP_OTHERGAMES));
    CHECK_APP("cdn.example-games.com", APP_OTHERGAMES);
    CHECK(apps_customAdd("example-games.com", APP_ROBLOX));    // same suffix: replaced, not duplicated
    CHECK_APP("example-games.com", APP_ROBLOX);
    CHECK(apps_customAdd("youtube.com", APP_OTHERVIDEO));      // the user's rule beats the built-in one
    CHECK_APP("www.youtube.com", APP_OTHERVIDEO);
    AppRule list[MAX_CUSTOM_RULES];
    CHECK(apps_customList(list, MAX_CUSTOM_RULES) == 2);
    CHECK(apps_customRemove("youtube.com") && !apps_customRemove("youtube.com"));
    CHECK_APP("www.youtube.com", APP_YOUTUBE);
    CHECK(apps_customRemove("example-games.com"));
    CHECK(!apps_customAdd("bad", APP_ROBLOX) && !apps_customAdd("ok.com", APP_COUNT));
    for (int i = 0; i < MAX_CUSTOM_RULES; i++) { char s[32]; snprintf(s, sizeof(s), "rule%d.example", i); CHECK(apps_customAdd(s, APP_EA)); }
    CHECK(!apps_customAdd("onetoomany.example", APP_EA));      // table full
    for (int i = 0; i < MAX_CUSTOM_RULES; i++) { char s[32]; snprintf(s, sizeof(s), "rule%d.example", i); apps_customRemove(s); }
}

static void testIpMemory() {
    apps_cacheClear();
    uint32_t yt = ip4(142, 250, 180, 14), other = ip4(8, 8, 4, 4);
    CHECK(apps_lookupIp(yt, 100) == APP_OTHER);
    apps_cachePut(yt, APP_YOUTUBE, 100);
    CHECK(apps_lookupIp(yt, 100) == APP_YOUTUBE);
    CHECK(apps_lookupIp(other, 100) == APP_OTHER);
    apps_cachePut(yt, APP_GOOGLE, 102);                        // generic must not steal a fresh specific address
    CHECK(apps_lookupIp(yt, 102) == APP_YOUTUBE);
    apps_cachePut(yt, APP_GOOGLE, 120);                        // ...but may once the entry is old
    CHECK(apps_lookupIp(yt, 120) == APP_GOOGLE);
    apps_cachePut(yt, APP_TIKTOK, 121);                        // a specific app always wins
    CHECK(apps_lookupIp(yt, 121) == APP_TIKTOK);
    CHECK(apps_lookupIp(yt, 121 + 361) == APP_OTHER);          // expired after 6 hours
    apps_cachePut(0, APP_YOUTUBE, 1);
    CHECK(apps_lookupIp(0, 1) == APP_OTHER);
    // Telegram: fixed ranges, no DNS needed.
    CHECK(apps_lookupIp(ip4(149, 154, 167, 51), 1) == APP_TELEGRAM);
    CHECK(apps_lookupIp(ip4(149, 154, 175, 100), 1) == APP_TELEGRAM);   // end of the /20
    CHECK(apps_lookupIp(ip4(149, 154, 176, 1), 1) == APP_OTHER);        // just outside
    CHECK(apps_lookupIp(ip4(91, 108, 4, 10), 1) == APP_TELEGRAM);
    CHECK(apps_lookupIp(ip4(91, 108, 7, 255), 1) == APP_TELEGRAM);
    CHECK(apps_lookupIp(ip4(91, 108, 24, 1), 1) == APP_OTHER);
    CHECK(apps_lookupIp(ip4(185, 76, 151, 9), 1) == APP_TELEGRAM);
    // Many addresses, 2-way sets: the table must never return a wrong app for an address it holds.
    apps_cacheClear();
    int wrong = 0;
    for (int i = 0; i < 400; i++) apps_cachePut(ip4(10, i / 250, i % 250, 7), (i % 2) ? APP_NETFLIX : APP_SPOTIFY, 5);
    for (int i = 0; i < 400; i++) {
        uint8_t a = apps_lookupIp(ip4(10, i / 250, i % 250, 7), 5);
        if (a != APP_OTHER && a != ((i % 2) ? APP_NETFLIX : APP_SPOTIFY)) wrong++;
    }
    CHECK(wrong == 0);
}

static void testDnsParse() {
    char name[96];
    auto q = makeQuery("WWW.Example.COM");
    CHECK(dns_parseQName(q.data(), (int)q.size(), name, sizeof(name)) && strcmp(name, "www.example.com") == 0);
    CHECK(!dns_parseQName(q.data(), 5, name, sizeof(name)));
    uint8_t junk[40]; memset(junk, 0x3F, sizeof(junk));         // label length 63 runs past the packet
    char tiny[8];
    CHECK(!dns_parseQName(junk, sizeof(junk), tiny, sizeof(tiny)) || tiny[0] != '\0');

    DnsLearn L;
    auto r1 = makeReply("www.youtube.com", {}, {ip4(142, 250, 180, 14), ip4(142, 250, 180, 46)});
    CHECK(dns_learnFromReply(r1.data(), (int)r1.size(), apps_classifyDomain("www.youtube.com"), L));
    CHECK(L.app == APP_YOUTUBE && L.nIps == 2 && L.ips[0] == ip4(142, 250, 180, 14) && L.ips[1] == ip4(142, 250, 180, 46));

    // The question is unknown but the CNAME chain reveals the app.
    auto r2 = makeReply("video.example.net", {"rr1---sn-abc.googlevideo.com"}, {ip4(172, 217, 1, 1)});
    CHECK(dns_learnFromReply(r2.data(), (int)r2.size(), APP_OTHER, L));
    CHECK(L.app == APP_YOUTUBE && L.nIps == 1);

    // A known question name is not overridden by a CNAME.
    auto r3 = makeReply("www.facebook.com", {"star.c10r.facebook.com"}, {ip4(157, 240, 1, 35)});
    CHECK(dns_learnFromReply(r3.data(), (int)r3.size(), APP_FACEBOOK, L) && L.app == APP_FACEBOOK);

    auto r4 = makeReply("nothing.example", {}, {ip4(1, 2, 3, 4)});
    CHECK(dns_learnFromReply(r4.data(), (int)r4.size(), APP_OTHER, L) && L.app == APP_OTHER && L.nIps == 1);

    // Not a reply, an error reply, and truncated packets must be rejected without reading past the end.
    auto bad = r1; bad[2] &= 0x7F;
    CHECK(!dns_learnFromReply(bad.data(), (int)bad.size(), APP_OTHER, L));
    auto nx = r1; nx[3] |= 3;
    CHECK(!dns_learnFromReply(nx.data(), (int)nx.size(), APP_OTHER, L));
    for (size_t cut = 0; cut < r2.size(); cut++) dns_learnFromReply(r2.data(), (int)cut, APP_OTHER, L);   // must not crash (run under -fsanitize in CI)
    CHECK(true);
    // Compression-pointer loop
    std::vector<uint8_t> loop = {0, 1, 0x81, 0x80, 0, 0, 0, 1, 0, 0, 0, 0, 0xC0, 12, 0, 5, 0, 1, 0, 0, 0, 60, 0, 2, 0xC0, 12};
    dns_learnFromReply(loop.data(), (int)loop.size(), APP_OTHER, L);
    CHECK(true);
    // More than 8 A records: only 8 kept
    std::vector<uint32_t> many; for (int i = 0; i < 12; i++) many.push_back(ip4(9, 9, 9, i + 1));
    auto r5 = makeReply("many.example", {}, many);
    CHECK(dns_learnFromReply(r5.data(), (int)r5.size(), APP_OTHER, L) && L.nIps == 8);
}

// Offer traffic much faster than the limit and check how much gets through.
static uint64_t shape(uint32_t kbps, int seconds, int offeredMbps, int pkt) {
    Bucket b = {};
    uint32_t rate = kbps * 125u;
    uint64_t passed = 0, now = 1000000;
    uint64_t gapUs = (uint64_t)pkt * 8 * 1000000 / ((uint64_t)offeredMbps * 1000000);
    for (uint64_t t = 0; t < (uint64_t)seconds * 1000000; t += gapUs) {
        now += gapUs;
        bucket_refill(b, rate, now);
        if (bucket_has(b, pkt)) { bucket_take(b, pkt); passed += pkt; }
    }
    return passed;
}

static void testShaper() {
    struct { uint32_t kbps; int offered; } cases[] = { {1000, 20}, {5000, 50}, {256, 10}, {20000, 100} };
    for (auto &c : cases) {
        uint64_t passed = shape(c.kbps, 10, c.offered, 1400);
        double expect = (double)c.kbps * 125 * 10 + bucket_burst(c.kbps * 125u);   // rate x time + one burst
        double err = ((double)passed - expect) / expect;
        printf("  limit %6u kbps offered %3d Mbit/s: passed %.2f Mbit/s (%.1f%% off the ideal)\n",
               c.kbps, c.offered, passed * 8.0 / 10 / 1e6, err * 100);
        CHECK(err > -0.06 && err < 0.06);
    }
    // Under the limit nothing is dropped.
    CHECK(shape(10000, 5, 2, 1400) >= (uint64_t)(2e6 / 8 * 5 * 0.99));
    // A long pause refills the bucket, but never beyond the burst.
    Bucket b = {}; bucket_refill(b, 125000, 0); bucket_take(b, b.tokens);
    bucket_refill(b, 125000, 3600ULL * 1000000ULL);
    CHECK(b.tokens == bucket_burst(125000));
    CHECK(bucket_burst(1000) == 8192 && bucket_burst(4000000) == 1000000);
}

int main() {
    testClassify();
    testCustomRules();
    testIpMemory();
    testDnsParse();
    testShaper();
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

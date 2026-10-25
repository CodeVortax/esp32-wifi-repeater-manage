#include "adblock.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

uint64_t adblock_hash64(const char *s, size_t n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 0x100000001b3ULL; }
    return h;
}
static inline uint32_t hash32(uint64_t h) { return (uint32_t)(h ^ (h >> 32)); }

// ---------------------------------------------------------------------------------------------
// Built-in list. Layout (little endian):
//   "BL" 1 | k | mbits | block | N(u32) | names(u32) | nBlocks(u16) |
//   nBlocks x { firstValue(5 bytes), bitOffset(3 bytes) } | Rice coded deltas
// A name is stored as hash64 % (names << mbits). Block j holds entries j*block ... ; its first
// value is in the index, the others are deltas from the one before.
// ---------------------------------------------------------------------------------------------
static const size_t HDR = 3 + 3 + 4 + 4 + 2;

static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint64_t rd40(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)p[4] << 32); }

uint32_t blocklist_count(const uint8_t *d, size_t len) {
    if (len < HDR || d[0] != 'B' || d[1] != 'L' || d[2] != 1) return 0;
    return rd32(d + 10);
}

bool blocklist_contains(const uint8_t *d, size_t len, uint64_t h) {
    if (blocklist_count(d, len) == 0) return false;
    const unsigned k = d[3], mbits = d[4], blk = d[5];
    const uint32_t n = rd32(d + 6), names = rd32(d + 10);
    const uint32_t nBlocks = d[14] | (d[15] << 8);
    if (n == 0 || blk == 0 || k > 31 || mbits > 24 || names == 0) return false;
    if (HDR + (size_t)nBlocks * 8 > len) return false;
    const uint8_t *index = d + HDR;
    const uint8_t *stream = index + (size_t)nBlocks * 8;
    const size_t streamBytes = len - (HDR + (size_t)nBlocks * 8);
    const uint64_t v = h % ((uint64_t)names << mbits);

    // last block whose first value is <= v
    int lo = 0, hi = (int)nBlocks - 1, found = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rd40(index + (size_t)mid * 8) <= v) { found = mid; lo = mid + 1; } else hi = mid - 1;
    }
    if (found < 0) return false;
    uint64_t cur = rd40(index + (size_t)found * 8);
    if (cur == v) return true;
    uint64_t bit = rd32(index + (size_t)found * 8 + 4) >> 8;        // 3-byte offset at bytes 5..7
    uint32_t inBlock = n - (uint32_t)found * blk;
    if (inBlock > blk) inBlock = blk;

    auto getBit = [&](uint64_t pos) -> int {
        if ((pos >> 3) >= streamBytes) return -1;
        return (stream[pos >> 3] >> (7 - (pos & 7))) & 1;
    };
    for (uint32_t i = 1; i < inBlock; i++) {
        uint64_t q = 0;
        for (;;) {
            int b = getBit(bit++);
            if (b < 0) return false;
            if (b == 0) break;
            if (++q > (1u << 20)) return false;                     // corrupt data
        }
        uint64_t r = 0;
        for (unsigned j = 0; j < k; j++) {
            int b = getBit(bit++);
            if (b < 0) return false;
            r = (r << 1) | (uint64_t)b;
        }
        cur += (q << k) | r;
        if (cur == v) return true;
        if (cur > v) return false;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// hosts file
// ---------------------------------------------------------------------------------------------
bool adblock_validHost(const char *s) {
    size_t n = strlen(s);
    if (n < 3 || n > 80 || strchr(s, '.') == nullptr || s[0] == '.' || s[n - 1] == '.' || s[0] == '-') return false;
    bool lastDot = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) return false;
        if (c == '.' && lastDot) return false;
        lastDot = (c == '.');
    }
    return true;
}

static bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Takes one line apart: fills names[] (lower case, at most `maxNames`), returns how many, and
// sets allow for "@@" lines. Lines it does not understand give 0 names and bad = true.
struct Rule { char name[84]; };
static int parseLine(const char *p, size_t n, Rule *names, int maxNames, bool &allow, bool &bad) {
    allow = false; bad = false;
    size_t hash = 0;
    while (hash < n && p[hash] != '#') hash++;                      // strip comment
    n = hash;
    while (n > 0 && isSpace(p[n - 1])) n--;
    while (n > 0 && isSpace(*p)) { p++; n--; }
    if (n == 0) return 0;
    if (p[0] == '!' ) return 0;                                      // Adblock comment
    if (n >= 2 && p[0] == '@' && p[1] == '@') { allow = true; p += 2; n -= 2; }
    if (n >= 2 && p[0] == '|' && p[1] == '|') { p += 2; n -= 2; }
    if (n > 0 && p[n - 1] == '^') n--;

    int count = 0;
    size_t i = 0;
    bool first = true;
    while (i < n) {
        while (i < n && isSpace(p[i])) i++;
        size_t s = i;
        while (i < n && !isSpace(p[i])) i++;
        if (i == s) break;
        size_t l = i - s;
        char tmp[84];
        if (l >= sizeof(tmp)) { bad = true; return 0; }
        for (size_t j = 0; j < l; j++) { char c = p[s + j]; tmp[j] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
        tmp[l] = '\0';
        if (first) {
            first = false;
            // "0.0.0.0 name" or "127.0.0.1 name": skip the address word when more words follow
            bool addr = true;
            for (size_t j = 0; j < l; j++) if (!((tmp[j] >= '0' && tmp[j] <= '9') || tmp[j] == '.' || tmp[j] == ':')) { addr = false; break; }
            if (addr) { if (i >= n) bad = true; continue; }          // an address word: skip it (alone = junk)
        }
        if (!adblock_validHost(tmp)) { bad = true; continue; }
        if (strcmp(tmp, "localhost") == 0 || strcmp(tmp, "broadcasthost") == 0) continue;
        if (count < maxNames) { strcpy(names[count].name, tmp); count++; }
    }
    return count;
}

static void sortUnique(uint32_t *a, uint16_t &n) {
    std::sort(a, a + n);
    n = (uint16_t)(std::unique(a, a + n) - a);
}

bool hosts_parse(const char *text, size_t len, UserLists &out, HostsStats &st) {
    out = UserLists();
    st = HostsStats();
    uint32_t *blk = (uint32_t *)malloc(sizeof(uint32_t) * MAX_USER_BLOCKS);
    uint32_t *alw = (uint32_t *)malloc(sizeof(uint32_t) * MAX_USER_ALLOWS);
    if (!blk || !alw) { free(blk); free(alw); return false; }
    uint16_t nb = 0, na = 0;
    Rule names[8];
    size_t pos = 0;
    while (pos < len) {
        size_t e = pos;
        while (e < len && text[e] != '\n') e++;
        bool allow, bad;
        int c = parseLine(text + pos, e - pos, names, 8, allow, bad);
        if (bad && c == 0) st.ignored++;
        for (int i = 0; i < c; i++) {
            uint32_t h = hash32(adblock_hash64(names[i].name, strlen(names[i].name)));
            if (allow) { if (na < MAX_USER_ALLOWS) alw[na++] = h; else st.full = true; }
            else       { if (nb < MAX_USER_BLOCKS) blk[nb++] = h; else st.full = true; }
        }
        pos = e + 1;
    }
    sortUnique(blk, nb);
    sortUnique(alw, na);
    st.blocks = nb; st.allows = na;
    out.block = blk; out.nBlock = nb;
    out.allow = alw; out.nAllow = na;
    return true;
}

void hosts_free(UserLists &l) {
    free(l.block); free(l.allow);
    l = UserLists();
}

// Copies every line except those that carry `domain` as their only name, then appends the rule.
size_t hosts_setRule(const char *text, size_t len, const char *domain, char mode, char *out, size_t cap) {
    size_t o = 0;
    size_t pos = 0;
    Rule names[8];
    while (pos < len) {
        size_t e = pos;
        while (e < len && text[e] != '\n') e++;
        bool allow, bad;
        int c = parseLine(text + pos, e - pos, names, 8, allow, bad);
        bool drop = (c == 1 && strcmp(names[0].name, domain) == 0);
        if (!drop) {
            size_t l = e - pos + (e < len ? 1 : 0);
            if (o + l + 1 > cap) return 0;
            memcpy(out + o, text + pos, e - pos);
            o += e - pos;
            if (e < len) out[o++] = '\n';
            else if (e > pos) out[o++] = '\n';                       // last line gets its newline
        }
        pos = e + 1;
    }
    if (mode == 'b' || mode == 'a') {
        char line[110];
        int l = snprintf(line, sizeof(line), mode == 'a' ? "@@%s\n" : "0.0.0.0 %s\n", domain);
        if (o + (size_t)l + 1 > cap) return 0;
        memcpy(out + o, line, l);
        o += l;
    }
    out[o] = '\0';
    return o;
}

static bool inSorted(const uint32_t *a, uint16_t n, uint32_t h) {
    return n && std::binary_search(a, a + n, h);
}

AdVerdict adblock_verdict(const char *domain, const UserLists &u, bool listOn, const uint8_t *data, size_t len) {
    size_t n = strlen(domain);
    if (n < 3 || n > 253) return AB_PASS;
    // Walk the name and every parent that still has two labels: a.b.c -> b.c
    AdVerdict result = AB_PASS;
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && domain[i - 1] != '.') continue;
        const char *s = domain + i;
        if (strchr(s, '.') == nullptr) break;
        uint64_t h = adblock_hash64(s, n - i);
        uint32_t h32 = hash32(h);
        if (inSorted(u.allow, u.nAllow, h32)) return AB_PASS;       // allow beats everything
        if (result == AB_PASS) {
            if (inSorted(u.block, u.nBlock, h32)) result = AB_USER;
            else if (listOn && blocklist_contains(data, len, h)) result = AB_LIST;
        }
    }
    return result;
}

// =============================================================================================
#ifdef ARDUINO
#include "config.h"
#include "blocklist_data.h"
#include <Preferences.h>
#include <nvs.h>

// The hosts file lives in NVS, not in the SPIFFS: an "update dashboard files" upload rewrites the
// whole SPIFFS and would delete it, while NVS survives every update. NVS also writes a blob
// atomically (the old one stays valid until the new one is complete).
#define NVS_NS   "adblock"
#define NVS_KEY  "hosts"

static UserLists          lists;
static bool               listOn = true;
static SemaphoreHandle_t  lock = nullptr;
static uint32_t           blockedTotal = 0;
static uint32_t           fileBytes = 0;

#define RECENT_MAX 24
#define PERCLIENT_MAX 16
static BlockedEntry recent[RECENT_MAX];
static int          recentN = 0;
struct PerClient { uint32_t ip; uint32_t count; };
static PerClient perClient[PERCLIENT_MAX];
static portMUX_TYPE statMux = portMUX_INITIALIZER_UNLOCKED;

static void swapLists(UserLists &fresh) {
    xSemaphoreTake(lock, portMAX_DELAY);
    UserLists old = lists;
    lists = fresh;
    xSemaphoreGive(lock);
    hosts_free(old);
    fresh = UserLists();
}

static bool reload(HostsStats &st) {
    String text = adblock_readFile();
    UserLists fresh;
    if (!hosts_parse(text.c_str(), text.length(), fresh, st)) return false;
    fileBytes = text.length();
    swapLists(fresh);
    return true;
}

void adblock_init() {
    lock = xSemaphoreCreateMutex();
    Preferences p;
    p.begin(NVS_NS, false);
    listOn = p.getBool("on", true);
    p.end();
    HostsStats st;
    reload(st);
    Serial.printf("[ADBLOCK] %s, built-in list %u names, your file: %u block / %u allow rules\n",
                  listOn ? "on" : "off", (unsigned)blocklist_count(BLOCKLIST_DATA, BLOCKLIST_BYTES), st.blocks, st.allows);
}

bool adblock_enabled() { return listOn; }

void adblock_setEnabled(bool on) {
    listOn = on;
    Preferences p;
    p.begin("adblock", false);
    p.putBool("on", on);
    p.end();
}

AdVerdict adblock_check(const char *domain) {
    if (!lock) return AB_PASS;
    xSemaphoreTake(lock, portMAX_DELAY);
    AdVerdict v = adblock_verdict(domain, lists, listOn, BLOCKLIST_DATA, BLOCKLIST_BYTES);
    xSemaphoreGive(lock);
    return v;
}

void adblock_noteBlocked(uint32_t ip, const char *domain, AdVerdict v) {
    portENTER_CRITICAL(&statMux);
    blockedTotal++;
    int idx = -1;
    for (int i = 0; i < recentN; i++)
        if (strncmp(recent[i].domain, domain, sizeof(recent[i].domain) - 1) == 0) { idx = i; break; }
    if (idx < 0) {
        if (recentN < RECENT_MAX) idx = recentN++;
        else { idx = 0; for (int i = 1; i < recentN; i++) if (recent[i].count < recent[idx].count) idx = i; }
        memset(&recent[idx], 0, sizeof(BlockedEntry));
        strncpy(recent[idx].domain, domain, sizeof(recent[idx].domain) - 1);
    }
    recent[idx].count++;
    recent[idx].clientIp = ip;
    recent[idx].verdict = v;
    int slot = -1;
    for (int i = 0; i < PERCLIENT_MAX; i++) if (perClient[i].ip == ip) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < PERCLIENT_MAX; i++) if (perClient[i].ip == 0) { slot = i; perClient[i].ip = ip; break; }
    if (slot >= 0) perClient[slot].count++;
    portEXIT_CRITICAL(&statMux);
}

uint32_t adblock_blockedFor(uint32_t ip) {
    uint32_t c = 0;
    portENTER_CRITICAL(&statMux);
    for (int i = 0; i < PERCLIENT_MAX; i++) if (perClient[i].ip == ip) { c = perClient[i].count; break; }
    portEXIT_CRITICAL(&statMux);
    return c;
}

void adblock_info(AdblockInfo &o) {
    o.enabled = listOn;
    o.listNames = blocklist_count(BLOCKLIST_DATA, BLOCKLIST_BYTES);
    xSemaphoreTake(lock, portMAX_DELAY);
    o.userBlocks = lists.nBlock; o.userAllows = lists.nAllow;
    xSemaphoreGive(lock);
    o.blockedTotal = blockedTotal;
    o.fileBytes = fileBytes;
}

int adblock_recent(BlockedEntry *out, int maxCount) {
    int n;
    portENTER_CRITICAL(&statMux);
    n = recentN < maxCount ? recentN : maxCount;
    memcpy(out, recent, n * sizeof(BlockedEntry));
    portEXIT_CRITICAL(&statMux);
    for (int i = 1; i < n; i++) {
        BlockedEntry k = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].count < k.count) { out[j + 1] = out[j]; j--; }
        out[j + 1] = k;
    }
    return n;
}

String adblock_readFile() {
    String s;
    Preferences p;
    p.begin(NVS_NS, false);                                       // read-write: no "NOT_FOUND" log on a first boot
    size_t n = p.getBytesLength(NVS_KEY);
    if (n > 0 && n <= HOSTS_FILE_MAX) {
        char *b = (char *)malloc(n + 1);
        if (b) {
            if (p.getBytes(NVS_KEY, b, n) == n) { b[n] = '\0'; s = b; }
            free(b);
        }
    }
    p.end();
    return s;
}

// Free room in NVS, in bytes (a blob needs its old copy kept until the new one is written).
uint32_t adblock_nvsFree() {
    nvs_stats_t st;
    if (nvs_get_stats(nullptr, &st) != ESP_OK) return 0;
    return st.free_entries * 30;
}

bool adblock_saveFile(const String &text, String &err, HostsStats &st) {
    if (text.length() > HOSTS_FILE_MAX) { err = "The file is too big (max 12 KB)."; return false; }
    if (adblock_nvsFree() < text.length() * 2 + 1024) { err = "Not enough free space on the repeater."; return false; }
    if (ESP.getMaxAllocHeap() < 16384) { err = "Not enough free memory right now. Try again."; return false; }

    UserLists test;
    if (!hosts_parse(text.c_str(), text.length(), test, st)) { err = "Out of memory."; return false; }
    hosts_free(test);

    Preferences p;
    if (!p.begin(NVS_NS, false)) { err = "Could not open the storage."; return false; }
    size_t w = text.length() ? p.putBytes(NVS_KEY, text.c_str(), text.length()) : (p.remove(NVS_KEY) ? 0 : 1);
    p.end();
    if (text.length() && w != text.length()) { err = "Writing failed. The previous file was kept."; return false; }
    HostsStats again;
    return reload(again);
}

bool adblock_setRule(const char *domain, char mode, String &err) {
    String d = domain;
    d.toLowerCase();
    d.trim();
    if (!adblock_validHost(d.c_str())) { err = "That is not a valid domain."; return false; }
    String text = adblock_readFile();
    size_t cap = text.length() + 160;
    char *buf = (char *)malloc(cap);
    if (!buf) { err = "Out of memory."; return false; }
    size_t n = hosts_setRule(text.c_str(), text.length(), d.c_str(), mode, buf, cap);
    if (n == 0 && !(mode == 'r' && text.length() == 0)) { free(buf); err = "The file is full."; return false; }
    String out(buf);
    free(buf);
    HostsStats st;
    return adblock_saveFile(out, err, st);
}
#endif

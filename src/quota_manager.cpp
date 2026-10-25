#include "quota_manager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>   // esp_netif_get_netif_impl()
#include <esp_timer.h>
#include <esp_crc.h>
#include <Preferences.h>
#include <time.h>

extern "C" {
  #include "lwip/netif.h"
  #include "lwip/pbuf.h"
}

// ---------------------------------------------------------------------------
// How counting and limiting work
//
// We hook the lwIP netif of the soft-AP and look at every Ethernet frame that
// crosses it:
//   input      (client -> ESP)  source MAC      = offset 6  -> upload
//   linkoutput (ESP -> client)  destination MAC = offset 0  -> download
// All NAT-ed internet traffic goes through here in both directions, only for
// stations really associated with our AP. For each frame we
//   1. look up which app the REMOTE address belongs to (apps_lookupIp: filled from
//      the DNS answers the dns task sees) and add the bytes to that app,
//   2. drop it if the device is blocked,
//   3. drop it if it is over the user's speed cap or the global cap (token buckets),
//   4. otherwise count it for the device, and block the device at its daily quota.
// ---------------------------------------------------------------------------

static DeviceUsage devices[MAX_TRACKED_DEVICES];
static uint64_t    appBytes[MAX_TRACKED_DEVICES][APP_COUNT];
static SpeedConfig speed = {0, 0, 0, 0};
static Bucket      gDl = {}, gUl = {};                 // the "everybody together" buckets
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool useDirty    = false;   // counters changed since the last save
static volatile bool useSaveNow  = false;   // save the counters at once (block, midnight)
static volatile bool cfgDirty    = false;   // names / limits / flags changed -> save at once
static int32_t       lastResetKey = -1;     // year*1000 + yday of the last reset
static bool          hooksOk      = false;
static uint8_t       nextUserNo   = 1;
static uint32_t      apIpNet      = 0;      // our own address: traffic to it is never limited

static netif_input_fn      origInput      = nullptr;
static netif_linkoutput_fn origLinkOutput = nullptr;

#define NS_SPEED NVS_QUOTA_NAMESPACE

// ---- MAC helpers ------------------------------------------------------------

static void macToStr(const uint8_t *mac, char *out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool parseMac(const char *s, uint8_t *out) {
    if (!s) return false;
    int v[6];
    char sep1, sep2, sep3, sep4, sep5;
    int n = sscanf(s, "%2x%c%2x%c%2x%c%2x%c%2x%c%2x",
                   &v[0], &sep1, &v[1], &sep2, &v[2], &sep3,
                   &v[3], &sep4, &v[4], &sep5, &v[5]);
    if (n != 11) return false;
    char seps[5] = {sep1, sep2, sep3, sep4, sep5};
    for (int i = 0; i < 5; i++) if (seps[i] != ':' && seps[i] != '-') return false;
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
    return true;
}

// ---- slot table (call with `mux` held) -------------------------------------

static int findSlotLocked(const uint8_t *mac) {
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++)
        if (devices[i].active && memcmp(devices[i].mac, mac, 6) == 0) return i;
    return -1;
}

static void initSlotLocked(int i, const uint8_t *mac, bool newUser) {
    memset(&devices[i], 0, sizeof(DeviceUsage));
    memset(appBytes[i], 0, sizeof(appBytes[i]));
    memcpy(devices[i].mac, mac, 6);
    macToStr(mac, devices[i].macStr);
    devices[i].dailyLimitMB = DAILY_QUOTA_MB_DEFAULT;
    devices[i].downKbps     = SPEED_DEFAULT;
    devices[i].upKbps       = SPEED_DEFAULT;
    devices[i].active       = true;
    devices[i].lastSeenMs   = millis();
    if (newUser) {
        devices[i].userNo = nextUserNo;
        nextUserNo = (nextUserNo >= 250) ? 1 : nextUserNo + 1;
        cfgDirty = true;
    }
}

static int findOrCreateLocked(const uint8_t *mac) {
    int i = findSlotLocked(mac);
    if (i >= 0) return i;

    for (i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (!devices[i].active) { initSlotLocked(i, mac, true); useDirty = true; return i; }
    }

    // Table full: recycle the least recently seen slot, preferring ones that
    // carry no manual settings (a name, the whitelist or a manual block).
    int victim = -1;
    for (i = 0; i < MAX_TRACKED_DEVICES; i++) {
        bool precious = devices[i].name[0] || devices[i].whitelisted || devices[i].blocked;
        if (precious) continue;
        if (victim < 0 || (int32_t)(devices[victim].lastSeenMs - devices[i].lastSeenMs) > 0) victim = i;
    }
    if (victim < 0) {
        victim = 0;
        for (i = 1; i < MAX_TRACKED_DEVICES; i++)
            if ((int32_t)(devices[victim].lastSeenMs - devices[i].lastSeenMs) > 0) victim = i;
    }
    initSlotLocked(victim, mac, true);
    useDirty = true;
    return victim;
}

// ---- accounting (runs in the lwIP / Wi-Fi task: keep it short) --------------

static inline uint16_t nowMinutes() { return (uint16_t)(millis() / 60000UL); }

static inline uint32_t rateBytesPerSec(uint32_t kbps, uint32_t defKbps) {
    uint32_t k = (kbps == SPEED_DEFAULT) ? defKbps : kbps;
    return k * 125u;                                   // kbit/s -> bytes/s
}

// Returns true when the frame must be dropped (blocked or over a speed cap).
//   frame/plen: first buffer of the frame (Ethernet header first), len: whole frame
static bool account(const uint8_t *frame, uint16_t plen, uint32_t len, bool upload) {
    const uint8_t *mac = upload ? frame + 6 : frame;
    if (mac[0] & 0x01) return false;               // broadcast / multicast

    // The remote address decides the app. Offsets: 12 ethertype, 14 IPv4 header, +12 src, +16 dst.
    uint32_t localIp = 0, remoteIp = 0;
    bool isIp4 = false;
    if (plen >= 34 && frame[12] == 0x08 && frame[13] == 0x00 && (frame[14] >> 4) == 4) {
        uint32_t s, d;
        memcpy(&s, frame + 26, 4);
        memcpy(&d, frame + 30, 4);
        isIp4    = true;
        localIp  = upload ? s : d;
        remoteIp = upload ? d : s;
    }
    bool toSelf = isIp4 && apIpNet != 0 && remoteIp == apIpNet;     // the dashboard, DNS, DHCP
    uint8_t app = (isIp4 && !toSelf) ? apps_lookupIp(remoteIp, nowMinutes()) : (uint8_t)APP_OTHER;
    uint64_t nowUs = (uint64_t)esp_timer_get_time();

    bool drop = false;
    portENTER_CRITICAL(&mux);
    int i = findOrCreateLocked(mac);
    if (i >= 0) {
        DeviceUsage &d = devices[i];
        d.lastSeenMs = millis();
        if (isIp4 && localIp != 0) d.ip = localIp;

        if (d.blocked && !d.whitelisted) {
            drop = true;
        } else {
            if (!d.whitelisted && !toSelf) {
                uint32_t devRate = rateBytesPerSec(upload ? d.upKbps : d.downKbps,
                                                   upload ? speed.defaultUpKbps : speed.defaultDownKbps);
                uint32_t allRate = (upload ? speed.globalUpKbps : speed.globalDownKbps) * 125u;
                Bucket &devB = upload ? d.ul : d.dl;
                Bucket &allB = upload ? gUl : gDl;
                if (devRate) bucket_refill(devB, devRate, nowUs);
                if (allRate) bucket_refill(allB, allRate, nowUs);
                if ((devRate && !bucket_has(devB, len)) || (allRate && !bucket_has(allB, len))) {
                    drop = true;                                    // over the cap: TCP will slow down
                } else {
                    if (devRate) bucket_take(devB, len);
                    if (allRate) bucket_take(allB, len);
                }
            }
            if (!drop) {
                d.bytesToday   += len;
                d.bytesAllTime += len;
                appBytes[i][app] += len;
                useDirty = true;
                if (!d.whitelisted && !d.quotaOverride && d.dailyLimitMB > 0 &&
                    d.bytesToday >= (uint64_t)d.dailyLimitMB * 1048576ULL) {
                    d.blocked     = true;
                    d.kickPending = true;
                    useSaveNow    = true;
                    cfgDirty      = true;
                }
            }
        }
    }
    portEXIT_CRITICAL(&mux);
    return drop;
}

static err_t hookedInput(struct pbuf *p, struct netif *inp) {
    if (p && p->len >= 14 && account((const uint8_t *)p->payload, p->len, p->tot_len, true)) {
        pbuf_free(p);
        return ERR_OK;
    }
    return origInput(p, inp);
}

static err_t hookedLinkOutput(struct netif *n, struct pbuf *p) {
    if (p && p->len >= 14 && account((const uint8_t *)p->payload, p->len, p->tot_len, false)) {
        return ERR_OK;                              // silently dropped
    }
    return origLinkOutput(n, p);
}

static void installHooks() {
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    struct netif *n = ap ? (struct netif *)esp_netif_get_netif_impl(ap) : nullptr;
    if (!n || !n->input || !n->linkoutput) {
        Serial.println("[QUOTA] could not find the AP netif - quota counting DISABLED");
        return;
    }
    if (n->input == hookedInput) { hooksOk = true; return; }   // already hooked
    origInput      = n->input;
    origLinkOutput = n->linkoutput;
    n->input       = hookedInput;
    n->linkoutput  = hookedLinkOutput;
    hooksOk = true;
    Serial.println("[QUOTA] AP traffic hooks installed");
}

bool quota_hooksInstalled() { return hooksOk; }

// ---- Wi-Fi events -----------------------------------------------------------

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event != ARDUINO_EVENT_WIFI_AP_STACONNECTED) return;
    portENTER_CRITICAL(&mux);
    int i = findOrCreateLocked(info.wifi_ap_staconnected.mac);
    if (i >= 0) devices[i].lastSeenMs = millis();
    portEXIT_CRITICAL(&mux);
}

// ---- persistence ------------------------------------------------------------
//
// Two blobs, each written in ONE NVS call (NVS makes that atomic, so a power cut
// leaves either the old blob or the new one) and each carrying a CRC32 as a second guard:
//   "cfg3"  names, limits, speeds, flags: tiny, written the moment you change something
//   "use3"  the counters: written at most once a minute, and only when they changed
// If a blob fails its checks it is ignored, and the other one still loads.

#define CFG_MAGIC 0x33474643u   // "CFG3"
#define USE_MAGIC 0x33455355u   // "USE3"

struct CfgHdr { uint32_t magic; uint32_t crc; uint16_t count; uint8_t nextUserNo; uint8_t pad; };
struct CfgRec {
    uint8_t  mac[6];
    uint8_t  flags;            // bit0 whitelisted, bit1 blocked
    uint8_t  userNo;
    uint32_t limitMB;
    uint32_t downKbps;
    uint32_t upKbps;
    char     name[NAME_MAX_BYTES + 1];
    uint8_t  pad[3];
};
struct UseHdr { uint32_t magic; uint32_t crc; int32_t dayKey; uint16_t count; uint16_t appCount; };
struct UseRec {
    uint8_t  mac[6];
    uint16_t pad;
    uint64_t today;
    uint64_t allTime;
    uint32_t appKB[APP_COUNT];
};

static void saveCfg() {
    CfgRec *recs = (CfgRec *)calloc(MAX_TRACKED_DEVICES, sizeof(CfgRec));
    if (!recs) return;
    int n = 0;
    uint8_t nu;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (!devices[i].active) continue;
        CfgRec &r = recs[n++];
        memcpy(r.mac, devices[i].mac, 6);
        r.flags    = (devices[i].whitelisted ? 1 : 0) | (devices[i].blocked ? 2 : 0);
        r.userNo   = devices[i].userNo;
        r.limitMB  = devices[i].dailyLimitMB;
        r.downKbps = devices[i].downKbps;
        r.upKbps   = devices[i].upKbps;
        memcpy(r.name, devices[i].name, sizeof(r.name));
    }
    nu = nextUserNo;
    cfgDirty = false;
    portEXIT_CRITICAL(&mux);

    size_t bytes = sizeof(CfgHdr) + n * sizeof(CfgRec);
    uint8_t *buf = (uint8_t *)malloc(bytes);
    if (buf) {
        CfgHdr h = {};
        h.magic = CFG_MAGIC; h.count = n; h.nextUserNo = nu;
        h.crc = esp_crc32_le(0, (const uint8_t *)recs, n * sizeof(CfgRec));
        memcpy(buf, &h, sizeof(h));
        memcpy(buf + sizeof(h), recs, n * sizeof(CfgRec));
        Preferences p;
        p.begin(NVS_QUOTA_NAMESPACE, false);
        p.putBytes("cfg3", buf, bytes);
        p.end();
        free(buf);
    }
    free(recs);
}

static void saveUse(int32_t dayKey) {
    UseRec *recs = (UseRec *)calloc(MAX_TRACKED_DEVICES, sizeof(UseRec));
    if (!recs) return;
    int n = 0;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (!devices[i].active) continue;
        UseRec &r = recs[n++];
        memcpy(r.mac, devices[i].mac, 6);
        r.today   = devices[i].bytesToday;
        r.allTime = devices[i].bytesAllTime;
        for (int a = 0; a < APP_COUNT; a++) {
            uint64_t kb = appBytes[i][a] >> 10;
            r.appKB[a] = kb > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)kb;
        }
    }
    useDirty = false;
    useSaveNow = false;
    portEXIT_CRITICAL(&mux);

    size_t bytes = sizeof(UseHdr) + n * sizeof(UseRec);
    uint8_t *buf = (uint8_t *)malloc(bytes);
    if (buf) {
        UseHdr h = {};
        h.magic = USE_MAGIC; h.dayKey = dayKey; h.count = n; h.appCount = APP_COUNT;
        h.crc = esp_crc32_le(0, (const uint8_t *)recs, n * sizeof(UseRec));
        memcpy(buf, &h, sizeof(h));
        memcpy(buf + sizeof(h), recs, n * sizeof(UseRec));
        Preferences p;
        p.begin(NVS_QUOTA_NAMESPACE, false);
        p.putBytes("use3", buf, bytes);
        p.end();
        free(buf);
    }
    free(recs);
}

// Reads a blob and checks size, magic and CRC. Caller frees the result.
static uint8_t *readBlob(Preferences &p, const char *key, uint32_t magic, size_t hdrSize, size_t recSize,
                         uint16_t &count, uint32_t &crcStored) {
    size_t bytes = p.getBytesLength(key);
    if (bytes < hdrSize || bytes > hdrSize + MAX_TRACKED_DEVICES * recSize) return nullptr;
    uint8_t *buf = (uint8_t *)malloc(bytes);
    if (!buf) return nullptr;
    if (p.getBytes(key, buf, bytes) != bytes) { free(buf); return nullptr; }
    uint32_t m; memcpy(&m, buf, 4);
    uint16_t c; memcpy(&c, buf + (magic == CFG_MAGIC ? 8 : 12), 2);
    memcpy(&crcStored, buf + 4, 4);
    if (m != magic || bytes != hdrSize + (size_t)c * recSize ||
        esp_crc32_le(0, buf + hdrSize, c * recSize) != crcStored) { free(buf); return nullptr; }
    count = c;
    return buf;
}

static int32_t loadFromNvs() {
    Preferences p;
    p.begin(NVS_QUOTA_NAMESPACE, false);     // read-write so a first boot does not log "nvs_open failed: NOT_FOUND"

    uint16_t count = 0; uint32_t crc = 0;
    uint8_t *cfg = readBlob(p, "cfg3", CFG_MAGIC, sizeof(CfgHdr), sizeof(CfgRec), count, crc);
    if (cfg) {
        CfgHdr h; memcpy(&h, cfg, sizeof(h));
        portENTER_CRITICAL(&mux);
        nextUserNo = h.nextUserNo ? h.nextUserNo : 1;
        for (int i = 0; i < count && i < MAX_TRACKED_DEVICES; i++) {
            CfgRec r; memcpy(&r, cfg + sizeof(CfgHdr) + i * sizeof(CfgRec), sizeof(r));
            initSlotLocked(i, r.mac, false);
            devices[i].whitelisted  = r.flags & 1;
            devices[i].blocked      = r.flags & 2;
            devices[i].userNo       = r.userNo;
            devices[i].dailyLimitMB = r.limitMB;
            devices[i].downKbps     = r.downKbps;
            devices[i].upKbps       = r.upKbps;
            r.name[NAME_MAX_BYTES] = '\0';
            memcpy(devices[i].name, r.name, sizeof(devices[i].name));
            devices[i].lastSeenMs   = 0;
        }
        portEXIT_CRITICAL(&mux);
        free(cfg);
    }

    int32_t day = -1;
    uint8_t *use = readBlob(p, "use3", USE_MAGIC, sizeof(UseHdr), sizeof(UseRec), count, crc);
    if (use) {
        UseHdr h; memcpy(&h, use, sizeof(h));
        day = h.dayKey;
        portENTER_CRITICAL(&mux);
        for (int k = 0; k < count; k++) {
            UseRec r; memcpy(&r, use + sizeof(UseHdr) + k * sizeof(UseRec), sizeof(r));
            int i = findSlotLocked(r.mac);
            if (i < 0) {                                           // counters without settings: still keep them
                for (i = 0; i < MAX_TRACKED_DEVICES && devices[i].active; i++) {}
                if (i >= MAX_TRACKED_DEVICES) continue;
                initSlotLocked(i, r.mac, true);
                devices[i].lastSeenMs = 0;
            }
            devices[i].bytesToday   = r.today;
            devices[i].bytesAllTime = r.allTime;
            if (h.appCount == APP_COUNT)
                for (int a = 0; a < APP_COUNT; a++) appBytes[i][a] = (uint64_t)r.appKB[a] << 10;
        }
        portEXIT_CRITICAL(&mux);
        free(use);
    }

    portENTER_CRITICAL(&mux);
    speed.globalDownKbps  = p.getULong("g_dn", 0);
    speed.globalUpKbps    = p.getULong("g_up", 0);
    speed.defaultDownKbps = p.getULong("d_dn", 0);
    speed.defaultUpKbps   = p.getULong("d_up", 0);
    portEXIT_CRITICAL(&mux);
    p.end();
    return day;
}

// ---- public API -------------------------------------------------------------

void quota_init() {
    memset(devices, 0, sizeof(devices));
    memset(appBytes, 0, sizeof(appBytes));
    lastResetKey = loadFromNvs();
    apIpNet = (uint32_t)WiFi.softAPIP();
    WiFi.onEvent(onWifiEvent);
    installHooks();
}

static void kickPendingStations() {
    uint8_t macs[MAX_TRACKED_DEVICES][6];
    int n = 0;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (devices[i].active && devices[i].kickPending) {
            memcpy(macs[n++], devices[i].mac, 6);
            devices[i].kickPending = false;
        }
    }
    portEXIT_CRITICAL(&mux);

    for (int i = 0; i < n; i++) {
        uint16_t aid = 0;
        if (esp_wifi_ap_get_sta_aid(macs[i], &aid) == ESP_OK && aid != 0) {
            esp_wifi_deauth_sta(aid);       // only THIS station, not everyone
            char s[18]; macToStr(macs[i], s);
            Serial.printf("[QUOTA] %s blocked - station disconnected\n", s);
        }
    }
}

void quota_loop() {
    kickPendingStations();

    // Midnight rollover. time() stays tiny until NTP has synced, so wait for it
    // (this also keeps loop() from blocking, unlike getLocalTime with a timeout).
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm t;
        localtime_r(&now, &t);
        int32_t key = (t.tm_year + 1900) * 1000 + t.tm_yday;
        if (lastResetKey != key) {
            // Also handles "rebooted across midnight": the saved key is older.
            if (lastResetKey != -1) quota_resetDaily();
            lastResetKey = key;
            useSaveNow = true;
        }
    }

    if (cfgDirty) saveCfg();

    static uint32_t lastUseSave = 0;
    if (useSaveNow || (useDirty && (millis() - lastUseSave) >= QUOTA_SAVE_INTERVAL_MS)) {
        saveUse(lastResetKey);
        lastUseSave = millis();
    }
}

void quota_saveNow() {
    saveCfg();
    saveUse(lastResetKey);
}

int quota_snapshot(DeviceUsage *out, int maxCount) {
    wifi_sta_list_t stations;
    memset(&stations, 0, sizeof(stations));
    if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK) stations.num = 0;

    int n = 0;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES && n < maxCount; i++) {
        if (!devices[i].active) continue;
        out[n] = devices[i];
        memset(out[n].catToday, 0, sizeof(out[n].catToday));
        for (int a = 0; a < APP_COUNT; a++) out[n].catToday[apps_category(a)] += appBytes[i][a];
        n++;
    }
    portEXIT_CRITICAL(&mux);

    for (int i = 0; i < n; i++) {
        out[i].online = false;
        for (int s = 0; s < stations.num; s++) {
            if (memcmp(stations.sta[s].mac, out[i].mac, 6) == 0) { out[i].online = true; break; }
        }
    }
    return n;
}

bool quota_appBytes(const char *macStr, uint64_t *out) {
    memset(out, 0, APP_COUNT * sizeof(uint64_t));
    uint8_t mac[6];
    if (macStr && !parseMac(macStr, mac)) return false;
    bool found = (macStr == nullptr);
    portENTER_CRITICAL(&mux);
    if (macStr) {
        int i = findSlotLocked(mac);
        if (i >= 0) { memcpy(out, appBytes[i], APP_COUNT * sizeof(uint64_t)); found = true; }
    } else {
        for (int i = 0; i < MAX_TRACKED_DEVICES; i++)
            if (devices[i].active) for (int a = 0; a < APP_COUNT; a++) out[a] += appBytes[i][a];
    }
    portEXIT_CRITICAL(&mux);
    return found;
}

bool quota_labelForIp(uint32_t ip, char *out, size_t outSize) {
    if (ip == 0 || outSize == 0) return false;
    bool found = false;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (devices[i].active && devices[i].ip == ip) {
            if (devices[i].name[0]) snprintf(out, outSize, "%s", devices[i].name);
            else                    snprintf(out, outSize, "User %u", devices[i].userNo);
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&mux);
    return found;
}

// Applies `fn` to the slot for macStr. Returns false when the device is unknown.
template <typename Fn>
static bool withDevice(const char *macStr, Fn fn) {
    uint8_t mac[6];
    if (!parseMac(macStr, mac)) return false;
    bool found = false;
    portENTER_CRITICAL(&mux);
    int i = findSlotLocked(mac);
    if (i >= 0) { fn(devices[i]); found = true; cfgDirty = true; }
    portEXIT_CRITICAL(&mux);
    return found;
}

bool quota_setLimit(const char *macStr, uint32_t mb) {
    return withDevice(macStr, [&](DeviceUsage &d) {
        d.dailyLimitMB  = mb;
        d.quotaOverride = false;
        // Raising the limit above current usage lifts an automatic block.
        if (mb == 0 || d.bytesToday < (uint64_t)mb * 1048576ULL) d.blocked = false;
    });
}

bool quota_block(const char *macStr) {
    return withDevice(macStr, [&](DeviceUsage &d) {
        if (d.whitelisted) return;
        d.blocked = true;
        d.quotaOverride = false;
        d.kickPending = true;
    });
}

bool quota_unblock(const char *macStr) {
    return withDevice(macStr, [&](DeviceUsage &d) {
        d.blocked = false;
        // Without this an over-quota device would be re-blocked on its very next frame.
        d.quotaOverride = true;
    });
}

bool quota_setWhitelist(const char *macStr, bool enabled) {
    return withDevice(macStr, [&](DeviceUsage &d) {
        d.whitelisted = enabled;
        if (enabled) d.blocked = false;
    });
}

bool quota_setName(const char *macStr, const char *name) {
    if (!name) return false;
    char clean[NAME_MAX_BYTES + 1];
    size_t n = 0;
    while (*name == ' ') name++;                       // trim the start
    for (; *name; name++) {
        uint8_t c = (uint8_t)*name;
        if (c < 0x20 || c == 0x7F) return false;       // no control characters
        if (n >= NAME_MAX_BYTES) return false;         // too long (UTF-8 bytes, not characters)
        clean[n++] = (char)c;
    }
    while (n > 0 && clean[n - 1] == ' ') n--;          // trim the end
    clean[n] = '\0';
    return withDevice(macStr, [&](DeviceUsage &d) {
        memcpy(d.name, clean, n + 1);
    });
}

static bool validSpeed(uint32_t v) { return v == SPEED_DEFAULT || v <= MAX_SPEED_KBPS; }

bool quota_setDeviceSpeed(const char *macStr, uint32_t downKbps, uint32_t upKbps) {
    if (!validSpeed(downKbps) || !validSpeed(upKbps)) return false;
    return withDevice(macStr, [&](DeviceUsage &d) {
        d.downKbps = downKbps;
        d.upKbps   = upKbps;
        d.dl.init  = false;                            // start with a full bucket under the new rate
        d.ul.init  = false;
    });
}

SpeedConfig quota_getSpeedConfig() {
    SpeedConfig c;
    portENTER_CRITICAL(&mux);
    c = speed;
    portEXIT_CRITICAL(&mux);
    return c;
}

void quota_setSpeedConfig(const SpeedConfig &in) {
    SpeedConfig c = in;
    if (c.globalDownKbps  > MAX_SPEED_KBPS) c.globalDownKbps  = MAX_SPEED_KBPS;
    if (c.globalUpKbps    > MAX_SPEED_KBPS) c.globalUpKbps    = MAX_SPEED_KBPS;
    if (c.defaultDownKbps > MAX_SPEED_KBPS) c.defaultDownKbps = MAX_SPEED_KBPS;
    if (c.defaultUpKbps   > MAX_SPEED_KBPS) c.defaultUpKbps   = MAX_SPEED_KBPS;
    portENTER_CRITICAL(&mux);
    speed = c;
    gDl.init = false;
    gUl.init = false;
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) { devices[i].dl.init = false; devices[i].ul.init = false; }
    portEXIT_CRITICAL(&mux);

    Preferences p;
    p.begin(NS_SPEED, false);
    p.putULong("g_dn", c.globalDownKbps);
    p.putULong("g_up", c.globalUpKbps);
    p.putULong("d_dn", c.defaultDownKbps);
    p.putULong("d_up", c.defaultUpKbps);
    p.end();
}

void quota_resetDaily() {
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        devices[i].bytesToday    = 0;
        devices[i].blocked       = false;
        devices[i].quotaOverride = false;
        memset(appBytes[i], 0, sizeof(appBytes[i]));
    }
    useDirty = true;
    useSaveNow = true;
    cfgDirty = true;
    portEXIT_CRITICAL(&mux);
    Serial.println("[QUOTA] daily counters reset");
}

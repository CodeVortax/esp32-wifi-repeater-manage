#include "quota_manager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>   // esp_netif_get_netif_impl()
#include <Preferences.h>
#include <time.h>

extern "C" {
  #include "lwip/netif.h"
  #include "lwip/pbuf.h"
}

// ---------------------------------------------------------------------------
// How counting works
//
// We hook the lwIP netif of the soft-AP and look at every Ethernet frame that
// crosses it:
//   input      (client -> ESP)  source MAC      = offset 6  -> upload
//   linkoutput (ESP -> client)  destination MAC = offset 0  -> download
// All NAT-ed internet traffic goes through here in both directions, only for
// stations really associated with our AP. A blocked station's frames are
// dropped right here, so blocking works even while it stays associated.
// ---------------------------------------------------------------------------

static DeviceUsage devices[MAX_TRACKED_DEVICES];
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool dirty         = false;   // counters changed since last save
static volatile bool saveRequested = false;   // config changed -> save soon
static int32_t       lastResetKey  = -1;      // year*1000 + yday of last reset
static bool          hooksOk       = false;

static netif_input_fn      origInput      = nullptr;
static netif_linkoutput_fn origLinkOutput = nullptr;

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

static void initSlotLocked(int i, const uint8_t *mac) {
    memset(&devices[i], 0, sizeof(DeviceUsage));
    memcpy(devices[i].mac, mac, 6);
    macToStr(mac, devices[i].macStr);
    devices[i].dailyLimitMB = DAILY_QUOTA_MB_DEFAULT;
    devices[i].active       = true;
    devices[i].lastSeenMs   = millis();
}

static int findOrCreateLocked(const uint8_t *mac) {
    int i = findSlotLocked(mac);
    if (i >= 0) return i;

    for (i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (!devices[i].active) { initSlotLocked(i, mac); dirty = true; return i; }
    }

    // Table full: recycle the least recently seen slot, preferring ones that
    // carry no manual settings (whitelist / manual block).
    int victim = -1;
    for (i = 0; i < MAX_TRACKED_DEVICES; i++) {
        bool precious = devices[i].whitelisted || devices[i].blocked;
        if (precious) continue;
        if (victim < 0 || (int32_t)(devices[victim].lastSeenMs - devices[i].lastSeenMs) > 0) victim = i;
    }
    if (victim < 0) {
        victim = 0;
        for (i = 1; i < MAX_TRACKED_DEVICES; i++)
            if ((int32_t)(devices[victim].lastSeenMs - devices[i].lastSeenMs) > 0) victim = i;
    }
    initSlotLocked(victim, mac);
    dirty = true;
    return victim;
}

// ---- accounting (runs in the lwIP / Wi-Fi task: keep it short) --------------

// Returns true when the frame must be dropped (device is blocked).
static bool account(const uint8_t *mac, uint32_t len) {
    if (mac[0] & 0x01) return false;               // broadcast / multicast

    bool drop = false;
    portENTER_CRITICAL(&mux);
    int i = findOrCreateLocked(mac);
    if (i >= 0) {
        DeviceUsage &d = devices[i];
        d.lastSeenMs = millis();
        if (d.blocked && !d.whitelisted) {
            drop = true;
        } else {
            d.bytesToday   += len;
            d.bytesAllTime += len;
            dirty = true;
            if (!d.whitelisted && !d.quotaOverride && d.dailyLimitMB > 0 &&
                d.bytesToday >= (uint64_t)d.dailyLimitMB * 1048576ULL) {
                d.blocked     = true;
                d.kickPending = true;
                saveRequested = true;
            }
        }
    }
    portEXIT_CRITICAL(&mux);
    return drop;
}

static err_t hookedInput(struct pbuf *p, struct netif *inp) {
    if (p && p->len >= 14 && account((const uint8_t *)p->payload + 6, p->tot_len)) {
        pbuf_free(p);
        return ERR_OK;
    }
    return origInput(p, inp);
}

static err_t hookedLinkOutput(struct netif *n, struct pbuf *p) {
    if (p && p->len >= 14 && account((const uint8_t *)p->payload, p->tot_len)) {
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

struct PersistRec {
    uint8_t  mac[6];
    uint8_t  flags;        // bit0 whitelisted, bit1 blocked
    uint8_t  pad;
    uint32_t limitMB;
    uint64_t today;
    uint64_t allTime;
};
#define PERSIST_VERSION 2

static void saveToNvs(int32_t dayKey) {
    PersistRec recs[MAX_TRACKED_DEVICES];
    int n = 0;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (!devices[i].active) continue;
        PersistRec &r = recs[n++];
        memset(&r, 0, sizeof(r));
        memcpy(r.mac, devices[i].mac, 6);
        r.flags   = (devices[i].whitelisted ? 1 : 0) | (devices[i].blocked ? 2 : 0);
        r.limitMB = devices[i].dailyLimitMB;
        r.today   = devices[i].bytesToday;
        r.allTime = devices[i].bytesAllTime;
    }
    dirty = false;
    saveRequested = false;
    portEXIT_CRITICAL(&mux);

    Preferences p;
    p.begin(NVS_QUOTA_NAMESPACE, false);
    p.putUChar("ver", PERSIST_VERSION);
    p.putInt("day", dayKey);
    if (n > 0) p.putBytes("devs", recs, n * sizeof(PersistRec));
    else       p.remove("devs");
    p.end();
}

static int32_t loadFromNvs() {
    Preferences p;
    p.begin(NVS_QUOTA_NAMESPACE, true);
    int32_t day = -1;
    if (p.getUChar("ver", 0) == PERSIST_VERSION) {
        day = p.getInt("day", -1);
        PersistRec recs[MAX_TRACKED_DEVICES];
        size_t bytes = p.getBytes("devs", recs, sizeof(recs));
        int n = bytes / sizeof(PersistRec);
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < n && i < MAX_TRACKED_DEVICES; i++) {
            initSlotLocked(i, recs[i].mac);
            devices[i].whitelisted  = recs[i].flags & 1;
            devices[i].blocked      = recs[i].flags & 2;
            devices[i].dailyLimitMB = recs[i].limitMB;
            devices[i].bytesToday   = recs[i].today;
            devices[i].bytesAllTime = recs[i].allTime;
            devices[i].lastSeenMs   = 0;
        }
        portEXIT_CRITICAL(&mux);
    }
    p.end();
    return day;
}

// ---- public API -------------------------------------------------------------

void quota_init() {
    memset(devices, 0, sizeof(devices));
    lastResetKey = loadFromNvs();
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
            saveRequested = true;
        }
    }

    static uint32_t lastSave = 0;
    bool due = (millis() - lastSave) > QUOTA_SAVE_INTERVAL_MS;
    if (saveRequested || (dirty && due)) {
        saveToNvs(lastResetKey);
        lastSave = millis();
    }
}

int quota_snapshot(DeviceUsage *out, int maxCount) {
    wifi_sta_list_t stations;
    memset(&stations, 0, sizeof(stations));
    if (esp_wifi_ap_get_sta_list(&stations) != ESP_OK) stations.num = 0;

    int n = 0;
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES && n < maxCount; i++) {
        if (devices[i].active) out[n++] = devices[i];
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

// Applies `fn` to the slot for macStr. Returns false when the device is unknown.
template <typename Fn>
static bool withDevice(const char *macStr, Fn fn) {
    uint8_t mac[6];
    if (!parseMac(macStr, mac)) return false;
    bool found = false;
    portENTER_CRITICAL(&mux);
    int i = findSlotLocked(mac);
    if (i >= 0) { fn(devices[i]); found = true; dirty = true; saveRequested = true; }
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

void quota_resetDaily() {
    portENTER_CRITICAL(&mux);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        devices[i].bytesToday    = 0;
        devices[i].blocked       = false;
        devices[i].quotaOverride = false;
    }
    dirty = true;
    saveRequested = true;
    portEXIT_CRITICAL(&mux);
    Serial.println("[QUOTA] daily counters reset");
}

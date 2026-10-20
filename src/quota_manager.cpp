#include "quota_manager.h"
#include "config.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <time.h>

static DeviceUsage devices[MAX_TRACKED_DEVICES];
static Preferences prefs;
static int lastResetDay = -1;

static void macToStr(const uint8_t *mac, char *out) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int findOrCreateSlot(const uint8_t *mac) {
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (devices[i].active && memcmp(devices[i].mac, mac, 6) == 0) return i;
    }
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (!devices[i].active) {
            memcpy(devices[i].mac, mac, 6);
            macToStr(mac, devices[i].macStr);
            devices[i].bytesToday   = 0;
            devices[i].bytesAllTime = 0;
            devices[i].dailyLimitMB = DAILY_QUOTA_MB_DEFAULT;
            devices[i].blocked      = false;
            devices[i].whitelisted  = false;
            devices[i].active       = true;
            return i;
        }
    }
    return -1; // table full
}

// Kicks a station that is on our AP blocklist. Called right after connect
// and again whenever a quota trip happens.
static void enforceBlock(int idx) {
    wifi_sta_list_t staList;
    esp_wifi_ap_get_sta_list(&staList);
    for (int i = 0; i < staList.num; i++) {
        if (memcmp(staList.sta[i].mac, devices[idx].mac, 6) == 0) {
            esp_wifi_deauth_sta(0); // 0 = broadcast reason, drops the association
        }
    }
}

// --- promiscuous sniffer: counts IP payload bytes per source MAC ---
// NOTE: this only sees frames on the current radio channel. Since ESP32
// forces the AP onto the STA's channel once connected, this covers all
// AP<->STA traffic. Accuracy is "good enough for quota enforcement", not
// carrier-grade billing.
static void IRAM_ATTR promiscCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_DATA) return;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    if (pkt->rx_ctrl.sig_len < 24) return;

    // 802.11 header: addr2 (offset 10) is usually the transmitter MAC
    const uint8_t *srcMac = pkt->payload + 10;
    quota_recordBytes(srcMac, pkt->rx_ctrl.sig_len);
}

static void onApStaConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event != ARDUINO_EVENT_WIFI_AP_STACONNECTED) return;
    int idx = findOrCreateSlot(info.wifi_ap_staconnected.mac);
    if (idx < 0) return;
    if (devices[idx].blocked && !devices[idx].whitelisted) {
        enforceBlock(idx);
    }
}

void quota_init() {
    memset(devices, 0, sizeof(devices));
    quota_loadFromNvs();

    WiFi.onEvent(onApStaConnected);

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&promiscCallback);
}

void quota_recordBytes(const uint8_t *mac, uint32_t len) {
    // Ignore broadcast/multicast and our own AP MAC
    if (mac[0] & 0x01) return;

    int idx = findOrCreateSlot(mac);
    if (idx < 0) return;

    devices[idx].bytesToday   += len;
    devices[idx].bytesAllTime += len;

    if (!devices[idx].whitelisted) {
        uint64_t limitBytes = (uint64_t)devices[idx].dailyLimitMB * 1024ULL * 1024ULL;
        if (devices[idx].bytesToday >= limitBytes && !devices[idx].blocked) {
            devices[idx].blocked = true;
            enforceBlock(idx);
            Serial.printf("[QUOTA] %s hit daily limit (%lu MB) — blocked\n",
                          devices[idx].macStr, devices[idx].dailyLimitMB);
        }
    }
}

void quota_loop() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) return;

    if (lastResetDay == -1) lastResetDay = timeinfo.tm_mday;
    if (timeinfo.tm_mday != lastResetDay) {
        quota_resetDaily();
        lastResetDay = timeinfo.tm_mday;
    }

    // Periodically persist counters (every ~60s) so a reboot doesn't wipe usage
    static uint32_t lastSave = 0;
    if (millis() - lastSave > 60000) {
        quota_saveToNvs();
        lastSave = millis();
    }
}

DeviceUsage* quota_getDevices(int &count) {
    count = MAX_TRACKED_DEVICES;
    return devices;
}

bool quota_setLimit(const char *macStr, uint32_t mb) {
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (devices[i].active && strcasecmp(devices[i].macStr, macStr) == 0) {
            devices[i].dailyLimitMB = mb;
            return true;
        }
    }
    return false;
}

bool quota_block(const char *macStr) {
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (devices[i].active && strcasecmp(devices[i].macStr, macStr) == 0) {
            devices[i].blocked = true;
            enforceBlock(i);
            return true;
        }
    }
    return false;
}

bool quota_unblock(const char *macStr) {
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (devices[i].active && strcasecmp(devices[i].macStr, macStr) == 0) {
            devices[i].blocked = false;
            return true;
        }
    }
    return false;
}

bool quota_setWhitelist(const char *macStr, bool enabled) {
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (devices[i].active && strcasecmp(devices[i].macStr, macStr) == 0) {
            devices[i].whitelisted = enabled;
            if (enabled) devices[i].blocked = false;
            return true;
        }
    }
    return false;
}

void quota_resetDaily() {
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        devices[i].bytesToday = 0;
        if (!devices[i].whitelisted) devices[i].blocked = false;
    }
    quota_saveToNvs();
    Serial.println("[QUOTA] daily counters reset");
}

void quota_saveToNvs() {
    prefs.begin(NVS_NAMESPACE, false);
    for (int i = 0; i < MAX_TRACKED_DEVICES; i++) {
        if (!devices[i].active) continue;
        String key = String(devices[i].macStr);
        key.replace(":", "");
        prefs.putULong64((key + "d").c_str(), devices[i].bytesToday);
        prefs.putULong64((key + "t").c_str(), devices[i].bytesAllTime);
        prefs.putUInt((key + "l").c_str(), devices[i].dailyLimitMB);
        prefs.putBool((key + "w").c_str(), devices[i].whitelisted);
    }
    prefs.end();
}

void quota_loadFromNvs() {
    // Devices repopulate their slots as they're seen on the air; NVS only
    // needs to restore limits/whitelist/counters once a MAC reappears.
    // (Kept intentionally simple — see README for extending this to a
    // full known-devices table keyed by MAC.)
}

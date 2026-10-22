#include "wifi_repeater.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <esp_netif.h>

extern "C" {
  #include "lwip/lwip_napt.h"
  #include "lwip/tcpip.h"        // LOCK_TCPIP_CORE / UNLOCK_TCPIP_CORE
}

static volatile bool staConnected = false;

// In AP+STA mode the radio has ONE channel. When the STA joins a router on a different channel the
// ESP32 silently moves the AP there, which drops every connected client (this is what kicked you out
// of the dashboard right after boot). So find the router's channel first and start the AP on it.
static uint8_t findUpstreamChannel(const String &ssid) {
    if (ssid.length() == 0) return 1;
    int n = WiFi.scanNetworks(false, false, false, 300);    // blocking, ~2-3 s, once at boot
    uint8_t ch = 1;
    int best = -1000;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ssid && WiFi.RSSI(i) > best) { best = WiFi.RSSI(i); ch = WiFi.channel(i); }
    }
    WiFi.scanDelete();
    Serial.printf("[AP] upstream \"%s\" %s, using channel %d\n", ssid.c_str(), best > -1000 ? "found" : "not found", ch);
    return ch;
}

// Hand out the ESP itself as the DNS server so the DNS monitor sees every
// lookup. Must run after the AP netif is up.
static void offerSelfAsDns() {
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap) return;

    esp_netif_dns_info_t dns;
    memset(&dns, 0, sizeof(dns));
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = (uint32_t)WiFi.softAPIP();

    esp_netif_dhcps_stop(ap);
    esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t offer = 1;   // OFFER_DNS
    esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    esp_netif_dhcps_start(ap);
}

static void macStr(const uint8_t *m, char *o) {
    snprintf(o, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    char mac[18];
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        staConnected = true;
        Serial.printf("[STA] connected, IP=%s channel=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.channel());
        // Enabling NAPT here (not once at boot) means the repeater also comes
        // up when the router is slow to answer, and again after a reconnect.
        // This handler runs in the Wi-Fi event task, not the lwIP task. lwIP is built with core locking,
        // and touching its timers without the lock trips an assert and reboots the chip (a boot loop
        // right after the router connects). Take the lock around the call.
        LOCK_TCPIP_CORE();
        ip_napt_enable((uint32_t)WiFi.softAPIP(), 1);
        UNLOCK_TCPIP_CORE();
        Serial.println("[NAT] NAPT enabled - repeater is live");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        // reason 2/15/202 = wrong password, 201 = router not found, 8 = router dropped us
        Serial.printf("[STA] disconnected, reason=%d%s\n", info.wifi_sta_disconnected.reason,
                      staConnected ? " (was connected)" : "");
        staConnected = false;
        break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
        macStr(info.wifi_ap_staconnected.mac, mac);
        Serial.printf("[AP] client joined %s (aid %d)\n", mac, info.wifi_ap_staconnected.aid);
        break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        macStr(info.wifi_ap_stadisconnected.mac, mac);
        Serial.printf("[AP] client left %s (aid %d, reason %d, channel now %d)\n", mac,
                      info.wifi_ap_stadisconnected.aid, info.wifi_ap_stadisconnected.reason, WiFi.channel());
        break;
    default:
        break;
    }
}

void wifiRepeater_init() {
    WiFi.persistent(false);              // don't rewrite credentials to flash on every boot
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);                // lower latency / higher throughput for a repeater

    String apSsid  = settings_getApSsid();
    String apPass  = settings_getApPassword();
    String staSsid = settings_getStaSsid();
    String staPass = settings_getStaPassword();

    uint8_t apChannel = findUpstreamChannel(staSsid);
    bool apOk = WiFi.softAP(apSsid.c_str(), apPass.c_str(), apChannel, 0, AP_MAX_CONN);
    if (!apOk) {
        // A bad stored AP password must never lock the dashboard away.
        Serial.println("[AP] start failed with saved settings, falling back to defaults");
        apOk = WiFi.softAP(AP_SSID, AP_PASSWORD, apChannel, 0, AP_MAX_CONN);
    }
    Serial.printf("[AP] %s SSID=%s IP=%s\n", apOk ? "up" : "FAILED",
                  apSsid.c_str(), WiFi.softAPIP().toString().c_str());

    offerSelfAsDns();

    WiFi.begin(staSsid.c_str(), staPass.c_str());
    Serial.printf("[STA] connecting to \"%s\" in the background\n", staSsid.c_str());
}

bool      wifiRepeater_isUpstreamConnected() { return staConnected; }
IPAddress wifiRepeater_getApIp()             { return WiFi.softAPIP(); }
IPAddress wifiRepeater_getStaIp()            { return WiFi.localIP(); }
uint8_t   wifiRepeater_getChannel()          { return WiFi.channel(); }
int       wifiRepeater_getRssi()             { return staConnected ? WiFi.RSSI() : 0; }
IPAddress wifiRepeater_getUpstreamDns()      { return staConnected ? WiFi.dnsIP(0) : IPAddress(0, 0, 0, 0); }

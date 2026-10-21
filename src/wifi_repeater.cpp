#include "wifi_repeater.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <esp_netif.h>

extern "C" {
  #include "lwip/lwip_napt.h"
}

static volatile bool staConnected = false;

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

static void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        staConnected = true;
        Serial.printf("[STA] connected, IP=%s channel=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.channel());
        // Enabling NAPT here (not once at boot) means the repeater also comes
        // up when the router is slow to answer, and again after a reconnect.
        ip_napt_enable((uint32_t)WiFi.softAPIP(), 1);
        Serial.println("[NAT] NAPT enabled - repeater is live");
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        if (staConnected) Serial.println("[STA] upstream lost, retrying...");
        staConnected = false;
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

    // Channel 1 until the STA locks one; the ESP32 then moves the AP onto the
    // upstream router's channel by itself.
    bool apOk = WiFi.softAP(apSsid.c_str(), apPass.c_str(), 1, 0, AP_MAX_CONN);
    if (!apOk) {
        // A bad stored AP password must never lock the dashboard away.
        Serial.println("[AP] start failed with saved settings, falling back to defaults");
        apOk = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, AP_MAX_CONN);
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

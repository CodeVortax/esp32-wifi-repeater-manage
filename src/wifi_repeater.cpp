#include "wifi_repeater.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>

extern "C" {
  #include "lwip/lwip_napt.h"
}

static bool staConnected = false;

void wifiRepeater_init() {
    WiFi.mode(WIFI_AP_STA);

    String apSsid = settings_getApSsid();
    String apPass = settings_getApPassword();
    String staSsid = settings_getStaSsid();
    String staPass = settings_getStaPassword();

    // Start AP first (channel 0 = auto until STA locks one; ESP32 forces
    // the AP onto the same channel as STA once connected).
    WiFi.softAP(apSsid.c_str(), apPass.c_str(), 1, 0, AP_MAX_CONN);
    Serial.printf("[AP] SSID=%s IP=%s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());

    WiFi.begin(staSsid.c_str(), staPass.c_str());
    Serial.print("[STA] connecting to upstream WiFi");
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(300);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        staConnected = true;
        Serial.printf("[STA] connected, IP=%s, channel=%d\n",
                       WiFi.localIP().toString().c_str(), WiFi.channel());

        // Enable NAT: AP subnet -> STA (internet). This is what makes it a repeater.
        ip_napt_enable(WiFi.softAPIP(), 1);
        Serial.println("[NAT] NAPT enabled — repeater is live");
    } else {
        staConnected = false;
        Serial.println("[STA] FAILED to connect upstream — AP has no internet yet");
    }
}

bool wifiRepeater_isUpstreamConnected() {
    return staConnected;
}

IPAddress wifiRepeater_getApIp() {
    return WiFi.softAPIP();
}

uint8_t wifiRepeater_getChannel() {
    return WiFi.channel();
}

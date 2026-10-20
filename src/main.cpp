#include <Arduino.h>
#include "settings.h"
#include "wifi_repeater.h"
#include "quota_manager.h"
#include "dns_monitor.h"
#include "web_server.h"
#include "telegram_bot.h"
#include "diagnostics.h"
#include <time.h>
#include "config.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    settings_init();
    wifiRepeater_init();

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    quota_init();
    dnsMonitor_init();
    diagnostics_init();
    webServer_init();
    telegramBot_init();

    Serial.println("[BOOT] all modules initialized");
}

void loop() {
    quota_loop();
    dnsMonitor_loop();
    telegramBot_loop();
    webServer_loop();

    static uint32_t lastBroadcast = 0;
    if (millis() - lastBroadcast > 2000) {
        webServer_broadcastStatus();
        lastBroadcast = millis();
    }
}

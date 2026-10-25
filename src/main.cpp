#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "settings.h"
#include "wifi_repeater.h"
#include "quota_manager.h"
#include "dns_monitor.h"
#include "diagnostics.h"
#include "web_server.h"
#include "telegram_bot.h"
#include "apps.h"
#include "ota.h"
#include "adblock.h"

// Hold BOOT (GPIO0) for FACTORY_RESET_HOLD_MS to wipe the saved network/login
// settings. This is the way back in if you forget the dashboard password.
static void checkFactoryReset() {
    static uint32_t pressedSince = 0;
    if (digitalRead(FACTORY_RESET_PIN) == LOW) {
        if (pressedSince == 0) pressedSince = millis();
        if (millis() - pressedSince >= FACTORY_RESET_HOLD_MS) {
            Serial.println("[RESET] BOOT held - clearing settings and restarting");
            settings_factoryReset();
            delay(200);
            ESP.restart();
        }
    } else {
        pressedSince = 0;
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n[BOOT] ESP32 WiFi Repeater %s\n", FW_VERSION);

    ota_init();                          // restores the previous firmware if a fresh update keeps crashing

    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);

    settings_init();
    wifiRepeater_init();                 // returns at once; NAPT turns on when the upstream is up

    configTzTime(TZ_INFO, NTP_SERVER);   // time zone + DST aware, so the daily reset fires at local midnight

    quota_init();                        // needs the AP netif, so after wifiRepeater_init()
    apps_loadCustom();                   // rules you taught it from the dashboard
    adblock_init();                      // your hosts file + the on/off switch; before the DNS task starts
    dnsMonitor_init();                   // background task
    diagnostics_init();
    webServer_init();
    telegramBot_init();                  // background task, only when configured

    Serial.println("[BOOT] all modules initialized");
}

void loop() {
    quota_loop();
    webServer_loop();
    ota_loop();
    checkFactoryReset();
    delay(20);
}

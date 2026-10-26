#pragma once
#include <Arduino.h>

// Updates over WiFi, from the dashboard (System > Update).
//   firmware: the app image (esp32-wifi-repeater-<ref>-ota-firmware.bin)
//   ui:       the dashboard files (esp32-wifi-repeater-<ref>-ota-ui.bin)
// Counters, names and WiFi settings live in NVS and survive both.
//
// Safety net: after a firmware update the new version must run OTA_HEALTHY_MS without
// rebooting. If it reboots 3 times first (a crash loop), the previous version, still in
// the other app slot, is started again.

void ota_init();             // first thing in setup()
void ota_loop();             // from loop(): confirms a healthy update

bool ota_begin(bool ui, String &err);
bool ota_write(const uint8_t *data, size_t len, String &err);
bool ota_end(bool ui, String &err);
void ota_abort();

bool ota_busy();                   // an update is being written right now

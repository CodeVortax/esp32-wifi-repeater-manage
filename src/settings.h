#pragma once
#include <Arduino.h>

// Seeds NVS with the compiled defaults from config.h on first boot.
// Call once in setup(), before wifiRepeater_init()/telegramBot_init().
void settings_init();

String settings_getStaSsid();
String settings_getStaPassword();
String settings_getApSsid();
String settings_getApPassword();
String settings_getTelegramToken();
String settings_getTelegramChatId();

// Any empty string argument means "leave that field unchanged".
// Returns true if at least one field was actually updated.
bool settings_update(const String &staSsid, const String &staPass,
                      const String &apSsid, const String &apPass,
                      const String &tgToken, const String &tgChatId);

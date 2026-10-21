#pragma once
#include <Arduino.h>

void   settings_init();

String settings_getStaSsid();
String settings_getStaPassword();
String settings_getApSsid();
String settings_getApPassword();
String settings_getAdminPassword();
String settings_getTelegramToken();
String settings_getTelegramChatId();

bool   settings_isDefaultAdminPassword();
bool   settings_telegramConfigured();

struct SettingsResult {
    bool   changed = false;
    String error;               // empty = ok
};

// Empty argument = "keep the current value". Every non-empty value is
// validated first; nothing is written if any value is invalid.
SettingsResult settings_update(const String &staSsid, const String &staPass,
                               const String &apSsid,  const String &apPass,
                               const String &adminPass,
                               const String &tgToken, const String &tgChatId);

void   settings_factoryReset();   // wipes network/login settings (not quota data)

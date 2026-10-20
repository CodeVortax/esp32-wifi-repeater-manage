#include "settings.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;
#define NS "netcfg"

static String getOrDefault(const char *key, const char *def) {
    prefs.begin(NS, true);
    String v = prefs.getString(key, def);
    prefs.end();
    return v;
}

void settings_init() {
    prefs.begin(NS, false);
    if (!prefs.isKey("ap_ssid"))  prefs.putString("ap_ssid", AP_SSID);
    if (!prefs.isKey("ap_pass"))  prefs.putString("ap_pass", AP_PASSWORD);
    if (!prefs.isKey("sta_ssid")) prefs.putString("sta_ssid", STA_SSID);
    if (!prefs.isKey("sta_pass")) prefs.putString("sta_pass", STA_PASSWORD);
    if (!prefs.isKey("tg_token")) prefs.putString("tg_token", TELEGRAM_BOT_TOKEN);
    if (!prefs.isKey("tg_chat"))  prefs.putString("tg_chat", TELEGRAM_CHAT_ID);
    prefs.end();
}

String settings_getStaSsid()       { return getOrDefault("sta_ssid", STA_SSID); }
String settings_getStaPassword()   { return getOrDefault("sta_pass", STA_PASSWORD); }
String settings_getApSsid()        { return getOrDefault("ap_ssid", AP_SSID); }
String settings_getApPassword()    { return getOrDefault("ap_pass", AP_PASSWORD); }
String settings_getTelegramToken() { return getOrDefault("tg_token", TELEGRAM_BOT_TOKEN); }
String settings_getTelegramChatId(){ return getOrDefault("tg_chat", TELEGRAM_CHAT_ID); }

bool settings_update(const String &staSsid, const String &staPass,
                      const String &apSsid, const String &apPass,
                      const String &tgToken, const String &tgChatId) {
    bool changed = false;
    prefs.begin(NS, false);

    auto setIfNotEmpty = [&](const char *key, const String &val) {
        if (val.length() > 0) {
            prefs.putString(key, val);
            changed = true;
        }
    };

    setIfNotEmpty("sta_ssid", staSsid);
    setIfNotEmpty("sta_pass", staPass);
    setIfNotEmpty("ap_ssid", apSsid);
    setIfNotEmpty("ap_pass", apPass);
    setIfNotEmpty("tg_token", tgToken);
    setIfNotEmpty("tg_chat", tgChatId);

    prefs.end();
    return changed;
}

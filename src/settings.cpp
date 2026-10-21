#include "settings.h"
#include "config.h"
#include <Preferences.h>

#define NS "netcfg"

// One short-lived Preferences handle per call. These functions are reached
// from the main task, the web server task and the Telegram task, so a shared
// static Preferences object would race.
static String readKey(const char *key, const char *def) {
    Preferences p;
    p.begin(NS, true);
    String v = p.getString(key, def);
    p.end();
    return v;
}

void settings_init() {
    Preferences p;
    p.begin(NS, false);
    if (!p.isKey("ap_ssid"))   p.putString("ap_ssid",   AP_SSID);
    if (!p.isKey("ap_pass"))   p.putString("ap_pass",   AP_PASSWORD);
    if (!p.isKey("sta_ssid"))  p.putString("sta_ssid",  STA_SSID);
    if (!p.isKey("sta_pass"))  p.putString("sta_pass",  STA_PASSWORD);
    if (!p.isKey("web_pass"))  p.putString("web_pass",  WEB_ADMIN_PASSWORD);
    if (!p.isKey("tg_token"))  p.putString("tg_token",  TELEGRAM_BOT_TOKEN);
    if (!p.isKey("tg_chat"))   p.putString("tg_chat",   TELEGRAM_CHAT_ID);
    p.end();
}

String settings_getStaSsid()        { return readKey("sta_ssid", STA_SSID); }
String settings_getStaPassword()    { return readKey("sta_pass", STA_PASSWORD); }
String settings_getApSsid()         { return readKey("ap_ssid",  AP_SSID); }
String settings_getApPassword()     { return readKey("ap_pass",  AP_PASSWORD); }
String settings_getAdminPassword()  { return readKey("web_pass", WEB_ADMIN_PASSWORD); }
String settings_getTelegramToken()  { return readKey("tg_token", TELEGRAM_BOT_TOKEN); }
String settings_getTelegramChatId() { return readKey("tg_chat",  TELEGRAM_CHAT_ID); }

bool settings_isDefaultAdminPassword() {
    return settings_getAdminPassword() == WEB_ADMIN_PASSWORD;
}

bool settings_telegramConfigured() {
    String tok = settings_getTelegramToken();
    String chat = settings_getTelegramChatId();
    return tok.length() > 20 && tok != TELEGRAM_BOT_TOKEN &&
           chat.length() > 0 && chat != TELEGRAM_CHAT_ID;
}

// ---- validation -------------------------------------------------------------

static bool printable(const String &s) {
    for (size_t i = 0; i < s.length(); i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x20 || c == 0x7F) return false;
    }
    return true;
}

static bool validChatId(const String &s) {
    if (s.length() == 0 || s.length() > 20) return false;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!(isdigit((unsigned char)c) || (i == 0 && c == '-'))) return false;
    }
    return true;
}

SettingsResult settings_update(const String &staSsid, const String &staPass,
                               const String &apSsid,  const String &apPass,
                               const String &adminPass,
                               const String &tgToken, const String &tgChatId) {
    SettingsResult r;

    // Validate everything before touching flash. A bad AP password would
    // otherwise make softAP() fail and lock you out of the dashboard.
    if (staSsid.length() && (staSsid.length() > 32 || !printable(staSsid)))
        { r.error = "Home WiFi name must be 1-32 characters"; return r; }
    if (staPass.length() && (staPass.length() < 8 || staPass.length() > 63 || !printable(staPass)))
        { r.error = "Home WiFi password must be 8-63 characters"; return r; }
    if (apSsid.length() && (apSsid.length() > 32 || !printable(apSsid)))
        { r.error = "Repeater name must be 1-32 characters"; return r; }
    if (apPass.length() && (apPass.length() < 8 || apPass.length() > 63 || !printable(apPass)))
        { r.error = "Repeater password must be 8-63 characters"; return r; }
    if (adminPass.length() && (adminPass.length() < 8 || adminPass.length() > 64 || !printable(adminPass)))
        { r.error = "Dashboard password must be 8-64 characters"; return r; }
    if (tgToken.length() && (tgToken.length() < 20 || tgToken.length() > 100 || tgToken.indexOf(':') < 0 || !printable(tgToken)))
        { r.error = "Telegram token looks wrong (expected 123456:ABC...)"; return r; }
    if (tgChatId.length() && !validChatId(tgChatId))
        { r.error = "Telegram chat ID must be a number"; return r; }

    Preferences p;
    p.begin(NS, false);

    // Only write (and only ask for a reboot) when a value really differs.
    auto setIfChanged = [&](const char *key, const String &val) {
        if (val.length() == 0) return;
        if (p.getString(key, "") == val) return;
        p.putString(key, val);
        r.changed = true;
    };

    setIfChanged("sta_ssid", staSsid);
    setIfChanged("sta_pass", staPass);
    setIfChanged("ap_ssid",  apSsid);
    setIfChanged("ap_pass",  apPass);
    setIfChanged("web_pass", adminPass);
    setIfChanged("tg_token", tgToken);
    setIfChanged("tg_chat",  tgChatId);

    p.end();
    return r;
}

void settings_factoryReset() {
    Preferences p;
    p.begin(NS, false);
    p.clear();
    p.end();
}

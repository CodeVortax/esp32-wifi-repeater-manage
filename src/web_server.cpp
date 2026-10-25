#include "web_server.h"
#include "config.h"
#include "quota_manager.h"
#include "dns_monitor.h"
#include "diagnostics.h"
#include "settings.h"
#include "wifi_repeater.h"
#include "apps.h"
#include "ota.h"
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include <time.h>
#include <new>

static AsyncWebServer server(WEB_SERVER_PORT);
static AsyncWebSocket ws("/ws");

static String adminPass;                  // loaded once; a password change reboots the device
static volatile bool restartPending = false;
static uint32_t restartAt = 0;

// ---- login sessions ----------------------------------------------------------
// The login page posts the password once and gets a random session cookie back; every
// other request must carry it. Sessions live in RAM only (a reboot logs everybody out).
// All of this runs in the async_tcp task, so it needs no lock.

struct Session { char token[33]; uint32_t lastUsed; bool used; };
static Session sessions[SESSION_MAX];

struct LoginTrack { uint32_t ip; uint8_t fails; uint32_t lockUntil; uint32_t lastTry; bool used; };
static LoginTrack tracks[4];

static bool ctEquals(const char *a, const char *b, size_t n) {       // constant time for equal lengths
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return diff == 0;
}

static bool ctEqualsStr(const String &a, const String &b) {
    size_t n = a.length() > b.length() ? a.length() : b.length();
    uint8_t diff = (uint8_t)(a.length() ^ b.length());
    for (size_t i = 0; i < n; i++) diff |= (uint8_t)(i < a.length() ? a[i] : 0) ^ (uint8_t)(i < b.length() ? b[i] : 0);
    return diff == 0;
}

static bool getCookie(AsyncWebServerRequest *req, const char *name, char *out, size_t outSize) {
    const AsyncWebHeader *h = req->getHeader("Cookie");
    if (!h) return false;
    const String &c = h->value();
    String key = String(name) + "=";
    int from = 0;
    for (;;) {
        int p = c.indexOf(key, from);
        if (p < 0) return false;
        if (p == 0 || c[p - 1] == ' ' || c[p - 1] == ';') {
            int s = p + key.length();
            int e = c.indexOf(';', s);
            if (e < 0) e = c.length();
            if ((size_t)(e - s) >= outSize) return false;
            memcpy(out, c.c_str() + s, e - s);
            out[e - s] = '\0';
            return true;
        }
        from = p + 1;
    }
}

static bool isAuthed(AsyncWebServerRequest *req) {
    char tok[48];
    if (!getCookie(req, "sid", tok, sizeof(tok)) || strlen(tok) != 32) return false;
    uint32_t now = millis();
    for (Session &s : sessions) {
        if (s.used && ctEquals(s.token, tok, 32)) {
            if (now - s.lastUsed > SESSION_TTL_MS) { s.used = false; return false; }
            s.lastUsed = now;
            return true;
        }
    }
    return false;
}

static void newToken(char *out) {
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) { out[i * 2] = hex[raw[i] >> 4]; out[i * 2 + 1] = hex[raw[i] & 15]; }
    out[32] = '\0';
}

static Session *createSession() {
    uint32_t now = millis();
    Session *pick = nullptr;
    for (Session &s : sessions) {
        if (!s.used || now - s.lastUsed > SESSION_TTL_MS) { pick = &s; break; }
        if (!pick || (int32_t)(pick->lastUsed - s.lastUsed) > 0) pick = &s;      // else replace the least recently used
    }
    newToken(pick->token);
    pick->lastUsed = now;
    pick->used = true;
    return pick;
}

static void endSession(AsyncWebServerRequest *req) {
    char tok[48];
    if (!getCookie(req, "sid", tok, sizeof(tok)) || strlen(tok) != 32) return;
    for (Session &s : sessions) if (s.used && ctEquals(s.token, tok, 32)) s.used = false;
}

static LoginTrack &trackFor(uint32_t ip) {
    LoginTrack *free = nullptr, *oldest = &tracks[0];
    for (LoginTrack &t : tracks) {
        if (t.used && t.ip == ip) return t;
        if (!t.used && !free) free = &t;
        if (t.used && (int32_t)(oldest->lastTry - t.lastTry) > 0) oldest = &t;
    }
    LoginTrack &t = free ? *free : *oldest;
    t = LoginTrack{ip, 0, 0, millis(), true};
    return t;
}

// ---- request helpers ----------------------------------------------------------

static void sendJson(AsyncWebServerRequest *req, int code, const String &body) {
    AsyncWebServerResponse *r = req->beginResponse(code, "application/json", body);
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
}

// Every API route needs the session cookie. State-changing calls also need a custom header:
// browsers will not add one cross-site without a CORS preflight, which (together with the
// SameSite=Strict cookie) stops other web pages from driving the API with your login.
static bool guard(AsyncWebServerRequest *req, bool mutating) {
    if (!isAuthed(req)) { sendJson(req, 401, "{\"error\":\"login\"}"); return false; }
    if (mutating && !req->hasHeader("X-Requested-With")) {
        req->send(403, "text/plain", "missing X-Requested-With header");
        return false;
    }
    return true;
}

static String param(AsyncWebServerRequest *req, const char *name) {
    return req->hasParam(name, true) ? req->getParam(name, true)->value() : String();
}

static bool parseUInt(const String &s, uint32_t maxValue, uint32_t &out) {
    if (s.length() == 0 || s.length() > 9) return false;
    for (size_t i = 0; i < s.length(); i++) if (!isdigit((unsigned char)s[i])) return false;
    uint32_t v = (uint32_t)s.toInt();
    if (v > maxValue) return false;
    out = v;
    return true;
}

// "default" -> SPEED_DEFAULT, otherwise kbit/s (0 = unlimited).
static bool parseSpeed(const String &s, uint32_t &out) {
    if (s == "default") { out = SPEED_DEFAULT; return true; }
    return parseUInt(s, MAX_SPEED_KBPS, out);
}

static void textReply(AsyncWebServerRequest *req, bool ok, const char *notFound = "device not found") {
    req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : notFound);
}

// ---- status JSON ------------------------------------------------------------

static void fillApps(JsonArray arr, const uint64_t *bytes) {
    for (int a = 0; a < APP_COUNT; a++) {
        uint64_t kb = bytes[a] >> 10;
        if (kb == 0) continue;
        JsonArray pair = arr.add<JsonArray>();
        pair.add(a);
        pair.add(kb);
    }
}

static String buildStatusJson() {
    JsonDocument doc;

    JsonObject sys = doc["sys"].to<JsonObject>();
    sys["version"]         = FW_VERSION;
    sys["uptime"]          = (uint32_t)(millis() / 1000);
    sys["heap"]            = ESP.getFreeHeap();
    sys["upstream"]        = wifiRepeater_isUpstreamConnected();
    sys["staSsid"]         = settings_getStaSsid();
    sys["apSsid"]          = settings_getApSsid();
    sys["staIp"]           = wifiRepeater_getStaIp().toString();
    sys["apIp"]            = wifiRepeater_getApIp().toString();
    sys["rssi"]            = wifiRepeater_getRssi();
    sys["channel"]         = wifiRepeater_getChannel();
    sys["counting"]        = quota_hooksInstalled();
    sys["defaultPassword"] = settings_isDefaultAdminPassword();
    sys["telegram"]        = settings_telegramConfigured();
    sys["time"]            = (uint32_t)time(nullptr);

    SpeedConfig sp = quota_getSpeedConfig();
    JsonObject speed = doc["speed"].to<JsonObject>();
    speed["globalDown"]  = sp.globalDownKbps;
    speed["globalUp"]    = sp.globalUpKbps;
    speed["defaultDown"] = sp.defaultDownKbps;
    speed["defaultUp"]   = sp.defaultUpKbps;

    // Heap, not stack: this runs from both the async_tcp task and loop().
    DeviceUsage *devs = new (std::nothrow) DeviceUsage[MAX_TRACKED_DEVICES];
    JsonArray arr = doc["devices"].to<JsonArray>();
    if (devs) {
        int n = quota_snapshot(devs, MAX_TRACKED_DEVICES);
        for (int i = 0; i < n; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["mac"]          = devs[i].macStr;
            o["name"]         = devs[i].name;
            o["userNo"]       = devs[i].userNo;
            o["ip"]           = devs[i].ip ? IPAddress(devs[i].ip).toString() : String("");
            o["bytesToday"]   = devs[i].bytesToday;
            o["bytesAllTime"] = devs[i].bytesAllTime;
            o["limitMB"]      = devs[i].dailyLimitMB;
            o["down"]         = devs[i].downKbps == SPEED_DEFAULT ? -1 : (int64_t)devs[i].downKbps;   // -1 = default
            o["up"]           = devs[i].upKbps   == SPEED_DEFAULT ? -1 : (int64_t)devs[i].upKbps;
            o["blocked"]      = devs[i].blocked;
            o["whitelisted"]  = devs[i].whitelisted;
            o["online"]       = devs[i].online;
            JsonArray cats = o["cats"].to<JsonArray>();
            for (int c = 0; c < CAT_COUNT; c++) cats.add(devs[i].catToday[c] >> 10);                   // KB per category
        }
        delete[] devs;
    }

    JsonArray dns = doc["dns"].to<JsonArray>();
    DnsLogEntry *entries = new (std::nothrow) DnsLogEntry[DNS_LOG_SIZE];
    if (entries) {
        int dn = dnsMonitor_snapshot(entries, DNS_LOG_SIZE);
        uint32_t nowMs = millis();
        for (int i = 0; i < dn; i++) {
            JsonObject o = dns.add<JsonObject>();
            char who[40];
            if (!quota_labelForIp((uint32_t)entries[i].clientIp, who, sizeof(who))) strcpy(who, "");
            o["ip"]     = entries[i].clientIp.toString();
            o["who"]    = who;
            o["domain"] = entries[i].domain;
            o["app"]    = entries[i].app;
            o["ageSec"] = (nowMs - entries[i].timestamp) / 1000;
        }
        delete[] entries;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

static void handleWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client,
                          AwsEventType type, void *, uint8_t *, size_t) {
    if (type == WS_EVT_CONNECT) client->text(buildStatusJson());
}

// ---- routes -----------------------------------------------------------------

// Every device action is "POST mac=..." and answers 200 / 404.
static void deviceAction(AsyncWebServerRequest *req, bool (*fn)(const char *)) {
    if (!guard(req, true)) return;
    String mac = param(req, "mac");
    if (mac.length() == 0) { req->send(400, "text/plain", "missing mac"); return; }
    textReply(req, fn(mac.c_str()));
}

static void sendPage(AsyncWebServerRequest *req, const char *path) {
    if (!SPIFFS.exists(path)) {
        req->send(500, "text/plain", "The dashboard files are missing. Flash the full image, or update the dashboard files from System > Update.");
        return;
    }
    AsyncWebServerResponse *r = req->beginResponse(SPIFFS, path, "text/html");
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
}

// OTA upload state (one upload at a time)
static String otaError;
static bool   otaOk = false;

void webServer_init() {
    adminPass = settings_getAdminPassword();

    if (!SPIFFS.begin(true)) Serial.println("[WEB] SPIFFS mount failed");

    DefaultHeaders::Instance().addHeader("X-Frame-Options", "DENY");
    DefaultHeaders::Instance().addHeader("X-Content-Type-Options", "nosniff");
    DefaultHeaders::Instance().addHeader("Referrer-Policy", "no-referrer");

    ws.setFilter([](AsyncWebServerRequest *req) { return isAuthed(req); });
    ws.onEvent(handleWsEvent);
    server.addHandler(&ws);

    // ---- pages ----
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!isAuthed(req)) { req->redirect("/login"); return; }
        sendPage(req, "/index.html");
    });
    server.on("/login", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (isAuthed(req)) { req->redirect("/"); return; }
        sendPage(req, "/login.html");
    });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *req) { req->send(204); });

    // ---- login / logout ----
    server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!req->hasHeader("X-Requested-With")) { req->send(403, "text/plain", "missing X-Requested-With header"); return; }
        LoginTrack &t = trackFor((uint32_t)req->client()->remoteIP());
        uint32_t now = millis();
        t.lastTry = now;
        if (t.lockUntil && (int32_t)(now - t.lockUntil) < 0) {
            sendJson(req, 429, String("{\"error\":\"locked\",\"retryIn\":") + ((t.lockUntil - now) / 1000 + 1) + "}");
            return;
        }
        bool ok = ctEqualsStr(param(req, "user"), String(WEB_ADMIN_USER)) & ctEqualsStr(param(req, "pass"), adminPass);
        if (!ok) {
            if (++t.fails >= LOGIN_MAX_FAILS) { t.fails = 0; t.lockUntil = now + LOGIN_LOCK_MS; }
            sendJson(req, 401, "{\"error\":\"bad\"}");
            return;
        }
        t.fails = 0;
        t.lockUntil = 0;
        Session *s = createSession();
        AsyncWebServerResponse *r = req->beginResponse(200, "application/json", "{\"ok\":true}");
        r->addHeader("Cache-Control", "no-store");
        r->addHeader("Set-Cookie", String("sid=") + s->token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + (SESSION_TTL_MS / 1000));
        req->send(r);
    });

    server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        endSession(req);
        AsyncWebServerResponse *r = req->beginResponse(200, "application/json", "{\"ok\":true}");
        r->addHeader("Set-Cookie", "sid=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
        req->send(r);
    });

    // ---- live data ----
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        sendJson(req, 200, buildStatusJson());
    });

    // Names of the categories and apps, in id order. Fetched once by the dashboard.
    server.on("/api/catalog", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        JsonDocument doc;
        JsonArray cats = doc["cats"].to<JsonArray>();
        for (int c = 0; c < CAT_COUNT; c++) cats.add(apps_catName(c));
        JsonArray apps = doc["apps"].to<JsonArray>();
        for (int a = 0; a < APP_COUNT; a++) {
            JsonArray p = apps.add<JsonArray>();
            p.add(apps_name(a));
            p.add(apps_category(a));
        }
        String out;
        serializeJson(doc, out);
        sendJson(req, 200, out);
    });

    // Per-app usage today, in KB: for everybody together and for each device.
    server.on("/api/apps", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        JsonDocument doc;
        uint64_t bytes[APP_COUNT];
        quota_appBytes(nullptr, bytes);
        fillApps(doc["total"].to<JsonArray>(), bytes);
        JsonObject perDevice = doc["devices"].to<JsonObject>();
        DeviceUsage *devs = new (std::nothrow) DeviceUsage[MAX_TRACKED_DEVICES];
        if (devs) {
            int n = quota_snapshot(devs, MAX_TRACKED_DEVICES);
            for (int i = 0; i < n; i++) {
                if (!quota_appBytes(devs[i].macStr, bytes)) continue;
                fillApps(perDevice[String(devs[i].macStr)].to<JsonArray>(), bytes);
            }
            delete[] devs;
        }
        String out;
        serializeJson(doc, out);
        sendJson(req, 200, out);
    });

    // ---- device actions ----
    server.on("/api/block",   HTTP_POST, [](AsyncWebServerRequest *req) { deviceAction(req, quota_block); });
    server.on("/api/unblock", HTTP_POST, [](AsyncWebServerRequest *req) { deviceAction(req, quota_unblock); });

    server.on("/api/setLimit", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        uint32_t mb;
        if (!parseUInt(param(req, "mb"), 10000000, mb)) {
            req->send(400, "text/plain", "mb must be 0 - 10000000 (0 = unlimited)"); return;
        }
        textReply(req, quota_setLimit(param(req, "mac").c_str(), mb));
    });

    server.on("/api/whitelist", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        textReply(req, quota_setWhitelist(param(req, "mac").c_str(), param(req, "enabled") == "1"));
    });

    server.on("/api/rename", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        if (!quota_setName(param(req, "mac").c_str(), param(req, "name").c_str())) {
            req->send(400, "text/plain", "device not found, or the name is longer than 32 bytes"); return;
        }
        req->send(200, "text/plain", "ok");
    });

    // One user's speed cap. down / up: "default", or kbit/s (0 = unlimited).
    server.on("/api/speed", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        uint32_t down, up;
        if (!parseSpeed(param(req, "down"), down) || !parseSpeed(param(req, "up"), up)) {
            req->send(400, "text/plain", "speed must be 'default' or 0 - 1000000 kbit/s"); return;
        }
        textReply(req, quota_setDeviceSpeed(param(req, "mac").c_str(), down, up));
    });

    // Global cap (everybody together) and the default cap per user.
    server.on("/api/speedDefaults", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        SpeedConfig c;
        if (!parseUInt(param(req, "gdown"), MAX_SPEED_KBPS, c.globalDownKbps) ||
            !parseUInt(param(req, "gup"),   MAX_SPEED_KBPS, c.globalUpKbps) ||
            !parseUInt(param(req, "ddown"), MAX_SPEED_KBPS, c.defaultDownKbps) ||
            !parseUInt(param(req, "dup"),   MAX_SPEED_KBPS, c.defaultUpKbps)) {
            req->send(400, "text/plain", "speeds must be 0 - 1000000 kbit/s (0 = unlimited)"); return;
        }
        quota_setSpeedConfig(c);
        req->send(200, "text/plain", "ok");
    });

    // ---- teaching the repeater new apps ----
    server.on("/api/learn", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        JsonDocument doc;
        JsonArray rules = doc["rules"].to<JsonArray>();
        AppRule list[MAX_CUSTOM_RULES];
        int rn = apps_customList(list, MAX_CUSTOM_RULES);
        for (int i = 0; i < rn; i++) {
            JsonObject o = rules.add<JsonObject>();
            o["suffix"] = list[i].suffix;
            o["app"]    = list[i].app;
        }
        JsonArray unknown = doc["unknown"].to<JsonArray>();
        UnknownDomain *u = new (std::nothrow) UnknownDomain[UNKNOWN_DOMAIN_MAX];
        if (u) {
            int un = dnsMonitor_unknownSnapshot(u, UNKNOWN_DOMAIN_MAX);
            for (int i = 0; i < un; i++) {
                JsonObject o = unknown.add<JsonObject>();
                char who[40];
                if (!quota_labelForIp(u[i].clientIp, who, sizeof(who))) strcpy(who, "");
                o["domain"] = u[i].domain;
                o["count"]  = u[i].count;
                o["who"]    = who;
            }
            delete[] u;
        }
        String out;
        serializeJson(doc, out);
        sendJson(req, 200, out);
    });

    server.on("/api/rules/add", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        uint32_t app;
        String suffix = param(req, "suffix");
        suffix.toLowerCase();
        if (!parseUInt(param(req, "app"), APP_COUNT - 1, app) || !apps_customAdd(suffix.c_str(), (uint8_t)app)) {
            req->send(400, "text/plain", "invalid domain, or the rule list is full (24 rules)"); return;
        }
        req->send(200, "text/plain", "ok");
    });
    server.on("/api/rules/del", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        textReply(req, apps_customRemove(param(req, "suffix").c_str()), "rule not found");
    });
    server.on("/api/unknown/clear", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        dnsMonitor_unknownClear();
        req->send(200, "text/plain", "ok");
    });

    // Ping is asynchronous: POST starts it, GET polls the result.
    server.on("/api/ping", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        String host = param(req, "host");
        if (host.length() == 0) host = "8.8.8.8";
        if (!diagnostics_pingStart(host)) {
            req->send(409, "text/plain", "invalid host, or a ping is already running"); return;
        }
        sendJson(req, 202, diagnostics_pingResult());
    });
    server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        sendJson(req, 200, diagnostics_pingResult());
    });

    // ---- settings: current SSIDs only. Passwords and the bot token are never sent back. ----
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        JsonDocument doc;
        doc["sta_ssid"]        = settings_getStaSsid();
        doc["ap_ssid"]         = settings_getApSsid();
        doc["telegram"]        = settings_telegramConfigured();
        doc["defaultPassword"] = settings_isDefaultAdminPassword();
        String out;
        serializeJson(doc, out);
        sendJson(req, 200, out);
    });

    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        SettingsResult r = settings_update(
            param(req, "sta_ssid"), param(req, "sta_password"),
            param(req, "ap_ssid"),  param(req, "ap_password"),
            param(req, "admin_password"),
            param(req, "tg_token"), param(req, "tg_chat_id"));

        if (r.error.length()) { req->send(400, "text/plain", r.error); return; }
        if (!r.changed)       { req->send(200, "text/plain", "no changes"); return; }
        req->send(200, "text/plain", "saved, rebooting...");
        restartPending = true;
        restartAt = millis() + 800;            // let the response flush first
    });

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        req->send(200, "text/plain", "rebooting...");
        restartPending = true;
        restartAt = millis() + 800;
    });

    // ---- update over WiFi: POST /api/ota?type=firmware|ui, multipart file ----
    server.on("/api/ota", HTTP_POST,
        [](AsyncWebServerRequest *req) {                         // runs once the whole upload arrived
            if (!isAuthed(req)) { sendJson(req, 401, "{\"error\":\"login\"}"); return; }
            if (!otaOk || otaError.length()) {
                String msg = otaError.length() ? otaError : String("no file received");
                msg.replace("\\", "\\\\"); msg.replace("\"", "'");
                sendJson(req, 400, String("{\"error\":\"") + msg + "\"}");
                return;
            }
            sendJson(req, 200, "{\"ok\":true}");
            restartPending = true;
            restartAt = millis() + 1500;
        },
        [](AsyncWebServerRequest *req, const String &, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {                                    // the first chunk carries the headers' verdict
                otaError = "";
                otaOk = false;
                if (!isAuthed(req))                          { otaError = "login required"; return; }
                if (!req->hasHeader("X-Requested-With"))     { otaError = "missing X-Requested-With header"; return; }
                bool ui = req->hasParam("type") && req->getParam("type")->value() == "ui";
                if (!ota_begin(ui, otaError)) return;
            }
            if (otaError.length()) return;                       // already failed: swallow the rest
            if (!ota_write(data, len, otaError)) return;
            if (final) {
                bool ui = req->hasParam("type") && req->getParam("type")->value() == "ui";
                otaOk = ota_end(ui, otaError);
            }
        });

    server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "not found"); });

    server.begin();
    Serial.println("[WEB] server started");
}

void webServer_loop() {
    if (restartPending && (int32_t)(millis() - restartAt) >= 0) {
        Serial.println("[WEB] restarting...");
        quota_saveNow();                               // nothing the counters gained since the last save is lost
        delay(100);
        ESP.restart();
    }

    static uint32_t lastPush = 0;
    if (millis() - lastPush >= 2000) {
        lastPush = millis();
        ws.cleanupClients();                           // drop dead sockets
        if (ws.count() > 0 && ws.availableForWriteAll()) ws.textAll(buildStatusJson());
    }
}

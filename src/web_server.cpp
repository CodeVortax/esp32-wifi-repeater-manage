#include "web_server.h"
#include "config.h"
#include "quota_manager.h"
#include "dns_monitor.h"
#include "diagnostics.h"
#include "settings.h"
#include "wifi_repeater.h"
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <time.h>
#include <new>

static AsyncWebServer server(WEB_SERVER_PORT);
static AsyncWebSocket ws("/ws");

static String adminPass;                  // loaded once; a password change reboots the device
static volatile bool restartPending = false;
static uint32_t restartAt = 0;

// ---- auth -------------------------------------------------------------------

// Basic auth on every route. For state-changing calls we also require a custom
// header: browsers will not add one cross-site without a CORS preflight, which
// stops other web pages from driving the API with your cached login.
static bool guard(AsyncWebServerRequest *req, bool mutating) {
    if (!req->authenticate(WEB_ADMIN_USER, adminPass.c_str())) {
        req->requestAuthentication();
        return false;
    }
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

// ---- status JSON ------------------------------------------------------------

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

    JsonArray arr = doc["devices"].to<JsonArray>();
    DeviceUsage devs[MAX_TRACKED_DEVICES];
    int n = quota_snapshot(devs, MAX_TRACKED_DEVICES);
    for (int i = 0; i < n; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["mac"]          = devs[i].macStr;
        o["bytesToday"]   = devs[i].bytesToday;
        o["bytesAllTime"] = devs[i].bytesAllTime;
        o["limitMB"]      = devs[i].dailyLimitMB;
        o["blocked"]      = devs[i].blocked;
        o["whitelisted"]  = devs[i].whitelisted;
        o["online"]       = devs[i].online;
    }

    JsonArray dns = doc["dns"].to<JsonArray>();
    // Heap, not static: this runs from both the async_tcp task and loop(), and
    // 4 KB is a lot for the async_tcp stack.
    DnsLogEntry *entries = new (std::nothrow) DnsLogEntry[DNS_LOG_SIZE];
    if (entries) {
        int dn = dnsMonitor_snapshot(entries, DNS_LOG_SIZE);
        uint32_t nowMs = millis();
        for (int i = 0; i < dn; i++) {
            JsonObject o = dns.add<JsonObject>();
            o["ip"]     = entries[i].clientIp.toString();
            o["domain"] = entries[i].domain;
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
    bool ok = fn(mac.c_str());
    req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : "device not found");
}

void webServer_init() {
    adminPass = settings_getAdminPassword();

    if (!SPIFFS.begin(true)) Serial.println("[WEB] SPIFFS mount failed");

    ws.setAuthentication(WEB_ADMIN_USER, adminPass.c_str());
    ws.onEvent(handleWsEvent);
    server.addHandler(&ws);

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        req->send(200, "application/json", buildStatusJson());
    });

    server.on("/api/block",   HTTP_POST, [](AsyncWebServerRequest *req) { deviceAction(req, quota_block); });
    server.on("/api/unblock", HTTP_POST, [](AsyncWebServerRequest *req) { deviceAction(req, quota_unblock); });

    server.on("/api/setLimit", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        uint32_t mb;
        if (!parseUInt(param(req, "mb"), 10000000, mb)) {
            req->send(400, "text/plain", "mb must be 0 - 10000000 (0 = unlimited)"); return;
        }
        bool ok = quota_setLimit(param(req, "mac").c_str(), mb);
        req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : "device not found");
    });

    server.on("/api/whitelist", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        bool ok = quota_setWhitelist(param(req, "mac").c_str(), param(req, "enabled") == "1");
        req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : "device not found");
    });

    // Ping is asynchronous: POST starts it, GET polls the result.
    server.on("/api/ping", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!guard(req, true)) return;
        String host = param(req, "host");
        if (host.length() == 0) host = "8.8.8.8";
        if (!diagnostics_pingStart(host)) {
            req->send(409, "text/plain", "invalid host, or a ping is already running"); return;
        }
        req->send(202, "application/json", diagnostics_pingResult());
    });
    server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        req->send(200, "application/json", diagnostics_pingResult());
    });

    // Current SSIDs only. Passwords and the bot token are never sent back.
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!guard(req, false)) return;
        JsonDocument doc;
        doc["sta_ssid"]        = settings_getStaSsid();
        doc["ap_ssid"]         = settings_getApSsid();
        doc["telegram"]        = settings_telegramConfigured();
        doc["defaultPassword"] = settings_isDefaultAdminPassword();
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
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

    server.serveStatic("/", SPIFFS, "/")
          .setDefaultFile("index.html")
          .setAuthentication(WEB_ADMIN_USER, adminPass.c_str());

    server.onNotFound([](AsyncWebServerRequest *req) { req->send(404, "text/plain", "not found"); });

    server.begin();
    Serial.println("[WEB] server started");
}

void webServer_loop() {
    if (restartPending && (int32_t)(millis() - restartAt) >= 0) {
        Serial.println("[WEB] settings changed, restarting...");
        ESP.restart();
    }

    static uint32_t lastPush = 0;
    if (millis() - lastPush >= 2000) {
        lastPush = millis();
        ws.cleanupClients();                           // drop dead sockets
        if (ws.count() > 0 && ws.availableForWriteAll()) ws.textAll(buildStatusJson());
    }
}

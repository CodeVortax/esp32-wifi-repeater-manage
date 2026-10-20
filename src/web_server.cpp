#include "web_server.h"
#include "config.h"
#include "quota_manager.h"
#include "dns_monitor.h"
#include "diagnostics.h"
#include "settings.h"
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

static AsyncWebServer server(WEB_SERVER_PORT);
static AsyncWebSocket ws("/ws");
static bool restartPending = false;
static uint32_t restartAt = 0;

static String buildStatusJson() {
    JsonDocument doc;
    JsonArray arr = doc["devices"].to<JsonArray>();

    int count;
    DeviceUsage *devs = quota_getDevices(count);
    for (int i = 0; i < count; i++) {
        if (!devs[i].active) continue;
        JsonObject o = arr.add<JsonObject>();
        o["mac"]          = devs[i].macStr;
        o["bytesToday"]   = devs[i].bytesToday;
        o["bytesAllTime"] = devs[i].bytesAllTime;
        o["limitMB"]      = devs[i].dailyLimitMB;
        o["blocked"]      = devs[i].blocked;
        o["whitelisted"]  = devs[i].whitelisted;
    }

    JsonArray dns = doc["dns"].to<JsonArray>();
    int dnsCount;
    DnsLogEntry *logEntries = dnsMonitor_getLog(dnsCount);
    for (int i = 0; i < dnsCount; i++) {
        if (logEntries[i].domain[0] == '\0') continue;
        JsonObject o = dns.add<JsonObject>();
        o["ip"]     = logEntries[i].clientIp.toString();
        o["domain"] = logEntries[i].domain;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

static void handleWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                           AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        client->text(buildStatusJson());
    }
}

void webServer_init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("[WEB] SPIFFS mount failed");
    }

    ws.onEvent(handleWsEvent);
    server.addHandler(&ws);

    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", buildStatusJson());
    });

    server.on("/api/setLimit", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("mac", true) || !req->hasParam("mb", true)) {
            req->send(400, "text/plain", "missing mac/mb"); return;
        }
        bool ok = quota_setLimit(req->getParam("mac", true)->value().c_str(),
                                  req->getParam("mb", true)->value().toInt());
        req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : "device not found");
    });

    server.on("/api/block", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("mac", true)) { req->send(400); return; }
        bool ok = quota_block(req->getParam("mac", true)->value().c_str());
        req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : "device not found");
    });

    server.on("/api/unblock", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("mac", true)) { req->send(400); return; }
        bool ok = quota_unblock(req->getParam("mac", true)->value().c_str());
        req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : "device not found");
    });

    server.on("/api/whitelist", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!req->hasParam("mac", true) || !req->hasParam("enabled", true)) {
            req->send(400); return;
        }
        bool enabled = req->getParam("enabled", true)->value() == "1";
        bool ok = quota_setWhitelist(req->getParam("mac", true)->value().c_str(), enabled);
        req->send(ok ? 200 : 404, "text/plain", ok ? "ok" : "device not found");
    });

    server.on("/api/ping", HTTP_GET, [](AsyncWebServerRequest *req) {
        String host = req->hasParam("host") ? req->getParam("host")->value() : "8.8.8.8";
        String result = diagnostics_ping(host.c_str());
        req->send(200, "application/json", result);
    });

    // Current SSIDs only — passwords/token are never echoed back over the
    // network. Fields left blank in the form on save just mean "keep as is".
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["sta_ssid"] = settings_getStaSsid();
        doc["ap_ssid"] = settings_getApSsid();
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *req) {
        auto val = [&](const char *name) -> String {
            return req->hasParam(name, true) ? req->getParam(name, true)->value() : "";
        };
        bool changed = settings_update(
            val("sta_ssid"), val("sta_password"),
            val("ap_ssid"), val("ap_password"),
            val("tg_token"), val("tg_chat_id"));

        if (changed) {
            req->send(200, "text/plain", "saved, rebooting...");
            restartPending = true;
            restartAt = millis() + 800; // let the response flush first
        } else {
            req->send(200, "text/plain", "no changes");
        }
    });

    server.begin();
    Serial.println("[WEB] server started");
}

void webServer_broadcastStatus() {
    ws.textAll(buildStatusJson());
}

void webServer_loop() {
    if (restartPending && millis() >= restartAt) {
        Serial.println("[WEB] settings changed, restarting...");
        ESP.restart();
    }
}

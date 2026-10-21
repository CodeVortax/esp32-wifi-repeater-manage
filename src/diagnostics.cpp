#include "diagnostics.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "ping/ping_sock.h"
#include <lwip/inet.h>

enum PingState { PING_IDLE, PING_RUNNING, PING_DONE, PING_ERROR };

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile PingState state = PING_IDLE;
static char     pingHost[64];
static char     pingError[48];
static uint32_t sent, received, totalMs;

static void onPingSuccess(esp_ping_handle_t hdl, void *) {
    uint32_t elapsed = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    portENTER_CRITICAL(&mux);
    totalMs += elapsed;
    portEXIT_CRITICAL(&mux);
}

static void onPingEnd(esp_ping_handle_t hdl, void *) {
    uint32_t req = 0, rep = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &req, sizeof(req));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY,   &rep, sizeof(rep));
    portENTER_CRITICAL(&mux);
    sent = req;
    received = rep;
    state = PING_DONE;
    portEXIT_CRITICAL(&mux);
    esp_ping_delete_session(hdl);
}

static void fail(const char *msg) {
    portENTER_CRITICAL(&mux);
    strncpy(pingError, msg, sizeof(pingError) - 1);
    pingError[sizeof(pingError) - 1] = '\0';
    state = PING_ERROR;
    portEXIT_CRITICAL(&mux);
}

// Hostnames / IPs only: keeps the JSON we build trivially safe.
static bool validHost(const String &h) {
    if (h.length() == 0 || h.length() >= sizeof(pingHost)) return false;
    for (size_t i = 0; i < h.length(); i++) {
        char c = h[i];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == ':')) return false;
    }
    return true;
}

static void pingTask(void *) {
    // hostByName blocks on DNS, which is why this is not done in the web handler.
    IPAddress ip;
    if (!WiFi.hostByName(pingHost, ip)) { fail("could not resolve host"); vTaskDelete(nullptr); return; }

    ip_addr_t target;
    memset(&target, 0, sizeof(target));
    ip_addr_set_ip4_u32(&target, (uint32_t)ip);

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target;
    config.count       = 4;
    config.timeout_ms  = 1000;
    config.interval_ms = 250;

    esp_ping_callbacks_t cbs = {
        .cb_args         = nullptr,
        .on_ping_success = onPingSuccess,
        .on_ping_timeout = nullptr,
        .on_ping_end     = onPingEnd,
    };

    esp_ping_handle_t handle = nullptr;
    if (esp_ping_new_session(&config, &cbs, &handle) != ESP_OK || esp_ping_start(handle) != ESP_OK) {
        fail("could not start ping");
    }
    // The result arrives through onPingEnd(); this task has nothing left to do.
    vTaskDelete(nullptr);
}

void diagnostics_init() {}

bool diagnostics_pingStart(const String &host) {
    if (!validHost(host)) return false;
    bool ok = false;
    portENTER_CRITICAL(&mux);
    if (state != PING_RUNNING) {
        strncpy(pingHost, host.c_str(), sizeof(pingHost) - 1);
        pingHost[sizeof(pingHost) - 1] = '\0';
        sent = received = totalMs = 0;
        pingError[0] = '\0';
        state = PING_RUNNING;
        ok = true;
    }
    portEXIT_CRITICAL(&mux);
    if (!ok) return false;
    if (xTaskCreate(pingTask, "ping", 4096, nullptr, 1, nullptr) != pdPASS) { fail("out of memory"); return false; }
    return true;
}

String diagnostics_pingResult() {
    // Copy under the lock, allocate (String / JSON) outside of it.
    char host[sizeof(pingHost)], err[sizeof(pingError)];
    uint32_t s_sent, s_recv, s_total;
    portENTER_CRITICAL(&mux);
    PingState s = state;
    memcpy(host, pingHost, sizeof(host));
    memcpy(err, pingError, sizeof(err));
    s_sent = sent; s_recv = received; s_total = totalMs;
    portEXIT_CRITICAL(&mux);

    static const char *names[] = {"idle", "running", "done", "error"};
    JsonDocument doc;
    doc["state"] = names[s];
    if (s != PING_IDLE) doc["host"] = host;
    if (s == PING_DONE) {
        doc["sent"]     = s_sent;
        doc["received"] = s_recv;
        doc["lossPct"]  = s_sent ? (100 * (s_sent - s_recv) / s_sent) : 100;
        doc["avgMs"]    = s_recv ? (s_total / s_recv) : 0;
    }
    if (s == PING_ERROR) doc["error"] = err;
    String out;
    serializeJson(doc, out);
    return out;
}

String diagnostics_pingBlocking(const String &host) {
    if (!diagnostics_pingStart(host)) return "Invalid host, or a ping is already running";
    uint32_t start = millis();
    while (state == PING_RUNNING && millis() - start < 8000) vTaskDelay(pdMS_TO_TICKS(100));

    JsonDocument doc;
    deserializeJson(doc, diagnostics_pingResult());
    if (doc["state"] == "done")
        return String((const char *)doc["host"]) + ": " + String((uint32_t)doc["received"]) + "/" +
               String((uint32_t)doc["sent"]) + " replies, " + String((int)doc["lossPct"]) +
               "% loss, avg " + String((uint32_t)doc["avgMs"]) + " ms";
    if (doc["state"] == "error") return String("Ping failed: ") + (const char *)doc["error"];
    return "Ping timed out";
}

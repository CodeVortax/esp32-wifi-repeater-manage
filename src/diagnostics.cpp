#include "diagnostics.h"
#include "ping/ping_sock.h"
#include <lwip/inet.h>

struct PingStats {
    int sent = 0;
    int received = 0;
    uint32_t totalMs = 0;
    bool done = false;
};

static PingStats stats;

static void onPingSuccess(esp_ping_handle_t hdl, void *args) {
    uint32_t elapsed;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    stats.received++;
    stats.totalMs += elapsed;
}

static void onPingEnd(esp_ping_handle_t hdl, void *args) {
    stats.done = true;
}

void diagnostics_init() {
    // Nothing to pre-init; sessions are created per-call in diagnostics_ping().
}

String diagnostics_ping(const char *host) {
    stats = PingStats();

    ip_addr_t target;
    if (!ipaddr_aton(host, &target)) {
        return "{\"error\":\"invalid host\"}";
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target;
    config.count = 4;
    config.timeout_ms = 1000;
    config.interval_ms = 250;

    esp_ping_callbacks_t cbs = {
        .cb_args = nullptr,
        .on_ping_success = onPingSuccess,
        .on_ping_timeout = nullptr,
        .on_ping_end = onPingEnd,
    };

    esp_ping_handle_t handle;
    esp_ping_new_session(&config, &cbs, &handle);
    esp_ping_start(handle);

    stats.sent = config.count;
    uint32_t start = millis();
    while (!stats.done && millis() - start < 6000) {
        delay(50);
    }
    esp_ping_stop(handle);
    esp_ping_delete_session(handle);

    int lossPct = stats.sent ? (100 * (stats.sent - stats.received) / stats.sent) : 100;
    uint32_t avgMs = stats.received ? (stats.totalMs / stats.received) : 0;

    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"host\":\"%s\",\"sent\":%d,\"received\":%d,\"lossPct\":%d,\"avgMs\":%lu}",
             host, stats.sent, stats.received, lossPct, avgMs);
    return String(buf);
}

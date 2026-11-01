#include "events.h"
#include <time.h>

static EventRec    ring[EVENT_LOG_SIZE];
static uint16_t    head  = 0;      // next slot to write
static uint16_t    count = 0;
static portMUX_TYPE mux  = portMUX_INITIALIZER_UNLOCKED;

void events_add(uint8_t type, const uint8_t *mac, uint32_t arg) {
    EventRec r;
    memset(&r, 0, sizeof(r));
    time_t now = time(nullptr);
    r.epoch = now > 1700000000 ? (uint32_t)now : 0;
    r.upSec = millis() / 1000UL;
    r.arg   = arg;
    r.type  = type;
    if (mac) memcpy(r.mac, mac, 6);
    portENTER_CRITICAL(&mux);
    ring[head] = r;
    head = (head + 1) % EVENT_LOG_SIZE;
    if (count < EVENT_LOG_SIZE) count++;
    portEXIT_CRITICAL(&mux);
}

int events_snapshot(EventRec *out, int maxCount) {
    int n = 0;
    portENTER_CRITICAL(&mux);
    for (int k = 0; k < count && n < maxCount; k++) {
        int idx = ((int)head - 1 - k + EVENT_LOG_SIZE * 2) % EVENT_LOG_SIZE;
        out[n++] = ring[idx];
    }
    portEXIT_CRITICAL(&mux);
    return n;
}

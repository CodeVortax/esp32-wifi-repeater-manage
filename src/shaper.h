#pragma once
// Token-bucket rate limiter. Pure C++ so it can be unit-tested on a PC: make -C test
//
// The repeater cannot hold a packet back without a queue, so "limiting" means
// dropping what exceeds the rate. TCP notices the loss and slows down, which gives a
// steady speed close to the limit. Used once per user and once for everybody.
#include <stdint.h>

struct Bucket {
    uint32_t tokens;     // bytes that may pass right now
    uint64_t lastUs;     // when it was last topped up
    bool     init;
};

// Room for about a quarter of a second of traffic, and never less than a few packets.
static inline uint32_t bucket_burst(uint32_t bytesPerSec) {
    uint32_t b = bytesPerSec / 4;
    return b < 8192 ? 8192 : b;
}

static inline void bucket_refill(Bucket &b, uint32_t bytesPerSec, uint64_t nowUs) {
    uint32_t burst = bucket_burst(bytesPerSec);
    if (!b.init) { b.init = true; b.tokens = burst; b.lastUs = nowUs; return; }
    uint64_t dt = nowUs - b.lastUs;
    if (dt > 1000000ULL) dt = 1000000ULL;                      // an idle bucket is simply full
    b.lastUs = nowUs;
    uint64_t t = (uint64_t)b.tokens + (uint64_t)bytesPerSec * dt / 1000000ULL;
    b.tokens = t > burst ? burst : (uint32_t)t;
}

static inline bool bucket_has(const Bucket &b, uint32_t len)  { return b.tokens >= len; }
static inline void bucket_take(Bucket &b, uint32_t len)       { b.tokens -= len; }

#pragma once
#include <Arduino.h>

void   diagnostics_init();

// Non-blocking ping: returns false when the host is invalid or a ping is already
// running. Poll diagnostics_pingResult() for the outcome.
bool   diagnostics_pingStart(const String &host);

// {"state":"idle|running|done|error", "host":..., "sent":.., "received":..,
//  "lossPct":.., "avgMs":.., "error":..}
String diagnostics_pingResult();

// Blocking variant for the Telegram task (runs outside the web server task).
String diagnostics_pingBlocking(const String &host);

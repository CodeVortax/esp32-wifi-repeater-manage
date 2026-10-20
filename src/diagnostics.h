#pragma once
#include <Arduino.h>

void diagnostics_init();

// Runs a small ping sweep against host, returns a JSON string:
// {"host":"8.8.8.8","sent":4,"received":4,"lossPct":0,"avgMs":23}
String diagnostics_ping(const char *host);

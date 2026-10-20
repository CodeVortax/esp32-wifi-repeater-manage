#pragma once
#include <Arduino.h>
#include <IPAddress.h>

// Brings up AP+STA mode and enables NAPT so AP clients get internet
// through the STA (upstream) connection.
void wifiRepeater_init();

bool wifiRepeater_isUpstreamConnected();
IPAddress wifiRepeater_getApIp();
uint8_t wifiRepeater_getChannel();

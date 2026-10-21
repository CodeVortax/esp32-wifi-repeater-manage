#pragma once
#include <Arduino.h>
#include <IPAddress.h>

void      wifiRepeater_init();           // returns immediately; the upstream connects in the background
bool      wifiRepeater_isUpstreamConnected();
IPAddress wifiRepeater_getApIp();
IPAddress wifiRepeater_getStaIp();
uint8_t   wifiRepeater_getChannel();
int       wifiRepeater_getRssi();        // upstream signal in dBm, 0 when not connected
IPAddress wifiRepeater_getUpstreamDns(); // 0.0.0.0 when unknown

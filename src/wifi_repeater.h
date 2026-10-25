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

// Transmit power level: 0 = maximum (19.5 dBm), 1 = 17, 2 = 13, 3 = 8.5 dBm. Stored in flash.
// Higher power reaches further, but a weak USB supply can brown out under it - lower it if the
// board restarts by itself.
uint8_t   wifiRepeater_getTxLevel();
void      wifiRepeater_setTxLevel(uint8_t level);   // applies at once and saves

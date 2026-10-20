#pragma once

void webServer_init();   // call once in setup(); server is async, no loop() needed
void webServer_loop();   // call in main loop(); handles pending settings-change restart
void webServer_broadcastStatus(); // push live usage update to connected clients

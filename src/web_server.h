#pragma once

void webServer_init();
void webServer_loop();     // pushes status to WebSocket clients, handles the post-save reboot

#include "ota.h"
#include "config.h"
#include <Update.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static bool    firstChunk = true;
static bool    uiMode     = false;

static bool partitionHasImage(const esp_partition_t *part) {
    uint8_t magic = 0;
    return part && esp_partition_read(part, 0, &magic, 1) == ESP_OK && magic == 0xE9;
}

void ota_init() {
    Preferences p;
    p.begin("ota", false);
    if (p.getBool("pending", false)) {
        uint8_t tries = p.getUChar("tries", 0) + 1;
        if (tries > 3) {
            p.putBool("pending", false);
            p.putUChar("tries", 0);
            const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);   // the other app slot
            if (partitionHasImage(other) && esp_ota_set_boot_partition(other) == ESP_OK) {
                p.end();
                Serial.println("[OTA] the new firmware keeps crashing - going back to the previous one");
                delay(100);
                ESP.restart();
            }
            Serial.println("[OTA] the new firmware keeps crashing and there is no previous one to go back to");
        } else {
            p.putUChar("tries", tries);
        }
    }
    p.end();
}

void ota_loop() {
    static bool checked = false;
    if (checked || millis() < OTA_HEALTHY_MS) return;
    checked = true;
    Preferences p;
    p.begin("ota", false);
    if (p.getBool("pending", false)) {
        p.putBool("pending", false);
        p.putUChar("tries", 0);
        Serial.println("[OTA] new firmware confirmed healthy");
    }
    p.end();
}

void ota_abort() {
    if (Update.isRunning()) Update.abort();
}

bool ota_begin(bool ui, String &err) {
    ota_abort();                                         // a previous upload may have been cut off
    firstChunk = true;
    uiMode = ui;
    // The dashboard files are about to be overwritten. Nothing may read them meanwhile, so unmount
    // first (a web page reading them half-way through the erase could crash the board).
    if (ui) SPIFFS.end();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, ui ? U_SPIFFS : U_FLASH)) {
        err = String("cannot start the update: ") + Update.errorString();
        return false;
    }
    Serial.printf("[OTA] receiving %s\n", ui ? "dashboard files" : "firmware");
    return true;
}

bool ota_busy() { return Update.isRunning(); }

bool ota_write(const uint8_t *data, size_t len, String &err) {
    if (firstChunk) {
        firstChunk = false;
        // An app image starts with 0xE9. The "full image" used for the first USB flash starts
        // with blank flash, which must never be written into an app slot.
        if (!uiMode && (len == 0 || data[0] != 0xE9)) {
            err = "this is not a firmware update file. Use the file ending in -ota-firmware.bin, not the full image.";
            ota_abort();
            return false;
        }
    }
    if (Update.write(const_cast<uint8_t *>(data), len) != len) {
        err = String("write failed: ") + Update.errorString();
        ota_abort();
        return false;
    }
    return true;
}

bool ota_end(bool ui, String &err) {
    if (!Update.end(true)) {                             // checks the image before it is used
        err = String("the file is damaged or too big: ") + Update.errorString();
        return false;
    }
    if (!ui) {                                           // arm the crash-loop protection
        Preferences p;
        p.begin("ota", false);
        p.putBool("pending", true);
        p.putUChar("tries", 0);
        p.end();
    }
    Serial.println("[OTA] update written");
    return true;
}

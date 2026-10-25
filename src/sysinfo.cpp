#include "sysinfo.h"
#include <SPIFFS.h>
#include <nvs.h>
#include <esp_ota_ops.h>

void sysinfo_get(SysInfo &o) {
    memset(&o, 0, sizeof(o));
    o.heapTotal    = ESP.getHeapSize();
    o.heapFree     = ESP.getFreeHeap();
    o.heapMin      = ESP.getMinFreeHeap();
    o.heapMaxBlock = ESP.getMaxAllocHeap();
    o.flashChip    = ESP.getFlashChipSize();
    const esp_partition_t *run = esp_ota_get_running_partition();
    o.appSlot      = run ? run->size : 0;
    o.appUsed      = ESP.getSketchSize();
    o.spiffsTotal  = SPIFFS.totalBytes();
    o.spiffsUsed   = SPIFFS.usedBytes();
    nvs_stats_t st;
    if (nvs_get_stats(nullptr, &st) == ESP_OK) {
        o.nvsTotal = st.total_entries * 32;                 // an NVS entry is 32 bytes
        o.nvsUsed  = st.used_entries * 32;
    }
    o.cpuMHz       = ESP.getCpuFreqMHz();
}

bool sysinfo_lowMemory() {
    return ESP.getMaxAllocHeap() < 12 * 1024;
}

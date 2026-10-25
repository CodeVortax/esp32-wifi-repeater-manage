#pragma once
#include <Arduino.h>

// Memory and flash figures for the System page.
struct SysInfo {
    uint32_t heapTotal, heapFree, heapMin, heapMaxBlock;   // RAM
    uint32_t flashChip;                                     // whole flash chip
    uint32_t appSlot, appUsed;                              // the running firmware and the slot it lives in
    uint32_t spiffsTotal, spiffsUsed;                       // dashboard files
    uint32_t nvsTotal, nvsUsed;                             // saved settings, counters, hosts file
    uint32_t cpuMHz;
};
void sysinfo_get(SysInfo &out);

// True when RAM is so low that heavy work (building big replies, parsing uploads) should wait.
bool sysinfo_lowMemory();

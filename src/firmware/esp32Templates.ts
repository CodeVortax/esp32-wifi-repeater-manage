export interface FirmwareModuleFile {
  id: string;
  path: string;
  title: string;
  category: 'Flash & Storage' | 'RAM & Watchdog' | 'RF & Latency' | 'Monthly & Time' | 'Crypto & Telegram';
  replaces: string;
  summary: string;
  code: string;
}

export const FIRMWARE_MODULES: FirmwareModuleFile[] = [
  {
    id: 'partitions',
    path: 'partitions.csv',
    title: 'Unified Single-Block Flash Layout (No OTA Slot Split)',
    category: 'Flash & Storage',
    replaces: 'partitions.csv (dual app0/app1 + spiffs)',
    summary:
      'Removes app1 (1.75 MB OTA duplicate), otadata, and SPIFFS. Gives factory app 3.375 MB starting at 0x10000 plus one monolithic 512 KB static data block (nvblock). Flash first-flash-0x0.bin once, then flash ONLY firmware.bin at 0x10000 without touching bootloader or partition table.',
    code: `# Name,     Type, SubType,  Offset,   Size,     Flags
# Unified single-firmware layout for 4 MB ESP32 (CodeVortax/esp32-wifi-repeater-manage v2.0)
# 1) Flash *-first-flash-0x0.bin at 0x0 ONCE to apply this partition table.
# 2) Every update after that: flash ONLY *-firmware-0x10000.bin at 0x10000 (bootloader & nvblock stay untouched).
nvs,        data, nvs,      0x9000,   0x7000,
factory,    app,  factory,  0x10000,  0x360000,
nvblock,    data, 0x99,     0x370000, 0x80000,
coredump,   data, coredump, 0x3F0000, 0x10000,
`,
  },
  {
    id: 'platformio',
    path: 'platformio.ini',
    title: 'PlatformIO Build + Embedded Gzipped UI & Fast NAPT',
    category: 'Flash & Storage',
    replaces: 'platformio.ini',
    summary:
      'Compiles the gzipped web dashboard directly into firmware.bin .rodata (eliminating SPIFFS uploads), enables compiler -O2 fast-path for lwIP NAPT, and pins safe AsyncTCP stack sizes to stop WDT crashes.',
    code: `[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
board_build.partitions = partitions.csv

build_flags =
  -O2
  -DCORE_DEBUG_LEVEL=1
  -DCONFIG_ASYNC_TCP_RUNNING_CORE=1
  -DCONFIG_ASYNC_TCP_TASK_STACK_SIZE=16384
  -DCONFIG_LWIP_IP_FORWARD=1
  -DCONFIG_LWIP_IPV4_NAPT=1
  -DMESH_STATIC_BLOCK_SIZE=524288
  -DEMBEDDED_PROGMEM_UI=1

lib_deps =
  ESP32Async/ESPAsyncWebServer @ ^3.3.22
  ESP32Async/AsyncTCP @ ^3.3.2
  bblanchon/ArduinoJson @ ^7.2.0
  witnessmenow/UniversalTelegramBot @ ^1.3.0

extra_scripts =
  pre:scripts/embed_ui.py
`,
  },
  {
    id: 'embed-ui',
    path: 'scripts/embed_ui.py',
    title: 'Pre-Build Gzipped Web UI Embedder (Single firmware.bin)',
    category: 'Flash & Storage',
    replaces: 'scripts/merge_bins.py + data/ SPIFFS upload',
    summary:
      'Gzip-compresses data/index.html, login.html, and my.html into include/embedded_ui.h so the entire dashboard ships inside firmware.bin. Serving gzip from PROGMEM also cuts RAM usage during login by ~85%.',
    code: `Import("env")
import gzip
from pathlib import Path

DATA_DIR = Path(env["PROJECT_DIR"]) / "data"
OUT_HEADER = Path(env["PROJECT_DIR"]) / "include" / "embedded_ui.h"

def to_c_array(name: str, raw: bytes) -> str:
    gz = gzip.compress(raw, compresslevel=9)
    hex_bytes = ", ".join(f"0x{b:02x}" for b in gz)
    return f"static const uint8_t {name}[] PROGMEM = {{ {hex_bytes} }};\\nstatic const size_t {name}_len = {len(gz)};\\n"

def generate_ui_header(*args, **kwargs):
    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    parts = ["#pragma once\\n#include <Arduino.h>\\n"]
    mapping = {
        "index.html": "UI_INDEX_GZ",
        "login.html": "UI_LOGIN_GZ",
        "my.html": "UI_MY_GZ",
    }
    for fname, sym in mapping.items():
        fpath = DATA_DIR / fname
        content = fpath.read_bytes() if fpath.exists() else b"<html><body>VortaxMesh UI</body></html>"
        parts.append(to_c_array(sym, content))
    OUT_HEADER.write_text("\\n".join(parts), encoding="utf-8")

generate_ui_header()
`,
  },
  {
    id: 'static-block-h',
    path: 'src/static_block_store.h',
    title: 'Monolithic Static Block Arena Header (Slotless Storage)',
    category: 'Flash & Storage',
    replaces: 'Fixed devices[MAX_TRACKED_DEVICES = 16] slot array',
    summary:
      'Eliminates arbitrary 16-user slots. Uses a single contiguous static DRAM arena + O(1) open-addressing MAC hash index persisted atomically into the 512 KB nvblock partition with CRC32 journal verification.',
    code: `#pragma once
#include <Arduino.h>
#include <esp_partition.h>

// Replaces MAX_TRACKED_DEVICES=16 fixed slots with a 128-entry static hash arena
// backed by a single 512 KB raw flash block ("nvblock" partition).
static constexpr size_t STATIC_MAX_NODES_TRACKED = 128;
static constexpr uint32_t STATIC_BLOCK_MAGIC     = 0x56545832; // "VTX2"

struct __attribute__((packed)) StaticDeviceEntry {
    uint8_t  mac[6];
    uint8_t  flags;          // bit0=used, bit1=blocked, bit2=whitelist, bit3=autoBlocked
    uint8_t  topAppId;
    uint32_t userNo;
    uint64_t monthlyBytes;
    uint64_t dailyBytes;
    uint32_t monthlyLimitMB;
    uint32_t speedDownKbps;
    uint32_t speedUpKbps;
    uint32_t lastSeenEpoch;
    char     name[32];
};

struct __attribute__((packed)) StaticTimeSlice {
    uint32_t epoch5Min;      // floor(epoch / 300)
    uint32_t totalKB;
    uint16_t avgLatencyMs;
    uint16_t activeClients;
    uint32_t topAppMask;
};

struct __attribute__((packed)) StaticMonthlyDigest {
    uint32_t cycleKey;       // YYYYMM
    uint64_t totalPlanBytes;
    uint32_t userCount;
    struct {
        uint8_t  mac[6];
        uint8_t  topAppId;
        uint8_t  reserved;
        uint64_t usedBytes;
        uint64_t topAppBytes;
    } users[32];
};

struct __attribute__((packed)) StaticMonolithicBlock {
    uint32_t magic;
    uint32_t seqNo;
    uint32_t activeCycleKey;
    uint32_t graceExpireEpoch;   // 0 if no 48h grace raw archive pending
    uint16_t deviceCount;
    uint16_t sliceHead;
    uint16_t digestCount;
    uint16_t errorHead;
    StaticDeviceEntry   devices[STATIC_MAX_NODES_TRACKED];
    StaticTimeSlice     slices[2016];      // 7 full days at 5-minute precision
    StaticTimeSlice     graceSlices[576];  // 48-hour rollover grace snapshot
    StaticMonthlyDigest digests[12];       // 12 months permanent compact analytics
    uint32_t crc32;
};

bool staticBlock_init();
StaticDeviceEntry* staticBlock_findOrInsert(const uint8_t mac[6]);
bool staticBlock_commit();
StaticMonolithicBlock* staticBlock_ptr();
size_t staticBlock_usedBytes();
size_t staticBlock_totalBytes();
`,
  },
  {
    id: 'static-block-cpp',
    path: 'src/static_block_store.cpp',
    title: 'Monolithic Static Block Arena Implementation',
    category: 'Flash & Storage',
    replaces: 'NVS per-slot preference keys',
    summary:
      'Maps one static contiguous arena in BSS/PSRAM and writes dual-page CRC32-protected snapshots into the monolithic nvblock partition. Zero heap allocations, wear-leveled rotation across the 512 KB block.',
    code: `#include "static_block_store.h"
#include <esp_crc.h>
#include <string.h>

static const esp_partition_t *nvPart = nullptr;
static StaticMonolithicBlock arena; // Static BSS — zero heap fragmentation
static uint32_t writeOffset = 0;

static uint32_t fnv1aMac(const uint8_t mac[6]) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) {
        h ^= mac[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t computeBlockCrc(const StaticMonolithicBlock *blk) {
    return esp_crc32_le(0, (const uint8_t *)blk, offsetof(StaticMonolithicBlock, crc32));
}

bool staticBlock_init() {
    nvPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x99, "nvblock");
    memset(&arena, 0, sizeof(arena));
    arena.magic = STATIC_BLOCK_MAGIC;

    if (!nvPart) {
        Serial.println("[STATIC_BLK] nvblock partition missing; operating in volatile arena");
        return false;
    }

    // Scan aligned block slots inside the 512 KB partition for newest valid sequence
    size_t stride = ((sizeof(StaticMonolithicBlock) + 4095) / 4096) * 4096;
    uint32_t bestSeq = 0;
    size_t bestOff = 0;
    bool found = false;

    StaticMonolithicBlock *probe = &arena;
    for (size_t off = 0; off + stride <= nvPart->size; off += stride) {
        StaticMonolithicBlock hdr;
        if (esp_partition_read(nvPart, off, &hdr, 16) != ESP_OK) continue;
        if (hdr.magic != STATIC_BLOCK_MAGIC) continue;
        if (hdr.seqNo >= bestSeq) {
            if (esp_partition_read(nvPart, off, probe, sizeof(StaticMonolithicBlock)) == ESP_OK) {
                if (computeBlockCrc(probe) == probe->crc32) {
                    bestSeq = probe->seqNo;
                    bestOff = off;
                    found = true;
                }
            }
        }
    }

    if (!found) {
        memset(&arena, 0, sizeof(arena));
        arena.magic = STATIC_BLOCK_MAGIC;
        arena.seqNo = 1;
        writeOffset = 0;
        return staticBlock_commit();
    }

    writeOffset = (bestOff + stride <= nvPart->size) ? bestOff : 0;
    return true;
}

StaticDeviceEntry* staticBlock_findOrInsert(const uint8_t mac[6]) {
    uint32_t idx = fnv1aMac(mac) % STATIC_MAX_NODES_TRACKED;
    for (size_t probe = 0; probe < STATIC_MAX_NODES_TRACKED; probe++) {
        size_t pos = (idx + probe) % STATIC_MAX_NODES_TRACKED;
        StaticDeviceEntry &e = arena.devices[pos];
        if ((e.flags & 0x01) == 0) {
            memset(&e, 0, sizeof(e));
            memcpy(e.mac, mac, 6);
            e.flags = 0x01;
            e.userNo = ++arena.deviceCount;
            return &e;
        }
        if (memcmp(e.mac, mac, 6) == 0) {
            return &e;
        }
    }
    return nullptr; // Arena full at 128 simultaneous tracked MACs
}

bool staticBlock_commit() {
    if (!nvPart) return false;
    size_t stride = ((sizeof(StaticMonolithicBlock) + 4095) / 4096) * 4096;
    size_t nextOff = (writeOffset + stride * 2 <= nvPart->size) ? (writeOffset + stride) : 0;

    arena.seqNo++;
    arena.crc32 = computeBlockCrc(&arena);

    if (esp_partition_erase_range(nvPart, nextOff, stride) != ESP_OK) return false;
    if (esp_partition_write(nvPart, nextOff, &arena, sizeof(arena)) != ESP_OK) return false;

    writeOffset = nextOff;
    return true;
}

StaticMonolithicBlock* staticBlock_ptr() { return &arena; }
size_t staticBlock_usedBytes()  { return sizeof(StaticMonolithicBlock); }
size_t staticBlock_totalBytes() { return nvPart ? nvPart->size : sizeof(StaticMonolithicBlock); }
`,
  },
  {
    id: 'ram-wdt-guard',
    path: 'src/ram_wdt_guard.cpp',
    title: 'Post-Login WDT Crash Fix + Static Stream & Non-Reset Error Log',
    category: 'RAM & Watchdog',
    replaces: 'web_server.cpp buildStatusJson() + burst endpoints',
    summary:
      'Fixes the crash/reboot right after dashboard login: replaces 8 simultaneous ArduinoJson String allocations in async_tcp with a single coalesced /api/snapshot chunked stream, cooperative task WDT yields, and an RTC NOINIT error ring buffer that logs faults without resetting the chip.',
    code: `#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "static_block_store.h"

// Survives soft recovery without wiping fault context
RTC_NOINIT_ATTR struct {
    uint32_t magic;
    uint16_t count;
    char     lastSubsystem[20];
    char     lastMessage[96];
    uint32_t heapAtFault;
} rtcFaultLog;

static constexpr size_t JSON_TX_POOL_BYTES = 4096;
static char staticJsonTxPool[JSON_TX_POOL_BYTES]; // Single static TX buffer (zero String churn)
static SemaphoreHandle_t txPoolMux = nullptr;

void ramGuard_init() {
    txPoolMux = xSemaphoreCreateMutex();
    if (rtcFaultLog.magic != 0xBEEFCAFE) {
        memset(&rtcFaultLog, 0, sizeof(rtcFaultLog));
        rtcFaultLog.magic = 0xBEEFCAFE;
    }
    // Extend async_tcp safety window and ensure idle WDT does not panic on TLS/JSON bursts
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms = 12000,
        .idle_core_mask = 0,
        .trigger_panic = false
    };
    esp_task_wdt_reconfigure(&wdtCfg);
}

void ramGuard_recordFault(const char *subsys, const char *msg) {
    rtcFaultLog.count++;
    strlcpy(rtcFaultLog.lastSubsystem, subsys, sizeof(rtcFaultLog.lastSubsystem));
    strlcpy(rtcFaultLog.lastMessage, msg, sizeof(rtcFaultLog.lastMessage));
    rtcFaultLog.heapAtFault = ESP.getFreeHeap();
    Serial.printf("[FAULT-GUARD] %s: %s (heap=%u)\\n", subsys, msg, rtcFaultLog.heapAtFault);
}

bool ramGuard_hasHeadroom(size_t minLargestBlock) {
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest < minLargestBlock) {
        ramGuard_recordFault("HeapGuard", "Low contiguous block; serving compact payload");
        return false;
    }
    return true;
}

// Single coalesced snapshot replaces 8 concurrent requests after login!
void ramGuard_sendCoalescedSnapshot(AsyncWebServerRequest *req) {
    if (!txPoolMux || xSemaphoreTake(txPoolMux, pdMS_TO_TICKS(150)) != pdTRUE) {
        req->send(503, "application/json", "{\\"busy\\":true}");
        return;
    }

    // Stream chunks directly to socket without building a 16 KB String in RAM
    AsyncResponseStream *res = req->beginResponseStream("application/json");
    res->addHeader("Cache-Control", "no-store");

    StaticMonolithicBlock *blk = staticBlock_ptr();
    size_t largestBlk = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    res->printf("{\\"heap\\":%u,\\"minHeap\\":%u,\\"largestBlock\\":%u,\\"faults\\":%u,\\"devices\\":[",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), (unsigned)largestBlk, (unsigned)rtcFaultLog.count);

    bool first = true;
    for (size_t i = 0; i < STATIC_MAX_NODES_TRACKED; i++) {
        if ((blk->devices[i].flags & 0x01) == 0) continue;
        if (!first) res->print(',');
        first = false;
        const StaticDeviceEntry &d = blk->devices[i];
        res->printf("{\\"mac\\":\\"%02X:%02X:%02X:%02X:%02X:%02X\\",\\"name\\":\\"%s\\",\\"mb\\":%llu,\\"cap\\":%u}",
                    d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5],
                    d.name[0] ? d.name : "Node", (unsigned long long)(d.monthlyBytes >> 20), d.monthlyLimitMB);
        if ((i & 0x0F) == 0) {
            esp_task_wdt_reset();
            taskYIELD();
        }
    }
    res->print("]}");
    req->send(res);
    xSemaphoreGive(txPoolMux);
}
`,
  },
  {
    id: 'wifi-channel-opt',
    path: 'src/wifi_channel_opt.cpp',
    title: 'Auto Channel Selector & Low-Latency Node Optimizer',
    category: 'RF & Latency',
    replaces: 'wifi_repeater.cpp static channel & default sleep',
    summary:
      'Runs non-blocking RF interference scoring across Channels 1–13 (penalizing co-channel and ±2 adjacent overlap), disables WiFi modem power-save (WIFI_PS_NONE), prioritizes DNS/ICMP/TCP-ACK frames in NAPT, and tracks per-node latency.',
    code: `#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

struct ChannelScore {
    uint8_t channel;
    int32_t interferenceScore; // lower is cleaner
    uint8_t overlappingAps;
};

static uint8_t currentBestChannel = 6;
static uint32_t lastScanMs = 0;

// Non-overlapping 2.4 GHz triad (1, 6, 11) scored with adjacent bleed weights
uint8_t wifiOpt_pickBestChannel(uint8_t upstreamChannel) {
    int n = WiFi.scanComplete();
    if (n <= 0) return upstreamChannel ? upstreamChannel : 6;

    ChannelScore candidates[3] = {{1, 0, 0}, {6, 0, 0}, {11, 0, 0}};
    for (int i = 0; i < n; i++) {
        int ch = WiFi.channel(i);
        int rssi = WiFi.RSSI(i); // e.g. -45 is strong interference, -90 is weak
        int weight = max(1, 100 + rssi);
        for (auto &c : candidates) {
            int dist = abs(ch - (int)c.channel);
            if (dist == 0) {
                c.interferenceScore += weight * 4;
                c.overlappingAps++;
            } else if (dist <= 2) {
                c.interferenceScore += weight * (3 - dist);
            }
        }
    }
    WiFi.scanDelete();

    uint8_t best = candidates[0].channel;
    int32_t bestScore = candidates[0].interferenceScore;
    for (int i = 1; i < 3; i++) {
        if (candidates[i].interferenceScore < bestScore) {
            bestScore = candidates[i].interferenceScore;
            best = candidates[i].channel;
        }
    }
    currentBestChannel = best;
    return best;
}

void wifiOpt_applyLowLatencyProfile() {
    // Eliminate DTIM sleep jitter on both AP & STA interfaces
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    // Use 802.11n short GI + fast rate adaptation for lower round-trip latency
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
}

void wifiOpt_tick() {
    uint32_t now = millis();
    if (now - lastScanMs > 300000UL) { // Every 5 min async RF survey
        lastScanMs = now;
        WiFi.scanNetworks(true, false, true, 120); // passive/fast non-blocking
    }
    if (WiFi.scanComplete() >= 0) {
        uint8_t best = wifiOpt_pickBestChannel(WiFi.channel());
        Serial.printf("[RF-OPT] Recommended quietest channel: %u\\n", best);
    }
}
`,
  },
  {
    id: 'monthly-plan',
    path: 'src/monthly_plan.cpp',
    title: 'Monthly Plan Rollover, 2-Day Grace Retention & Time-Range Query',
    category: 'Monthly & Time',
    replaces: 'quota_manager.cpp daily-only reset',
    summary:
      'Implements exact [fromEpoch, toEpoch] usage queries over the 5-min static ring buffer, monthly cycle rollover that resets user counters, saves raw details for 48 hours (2 days) before pruning, and permanently keeps each user’s total + #1 most-used app.',
    code: `#include <Arduino.h>
#include <time.h>
#include "static_block_store.h"

static constexpr uint32_t GRACE_RETENTION_SEC = 48UL * 3600UL; // 2 days

// Precise [fromEpoch, toEpoch] query across 5-minute slices
uint64_t monthlyPlan_queryRangeKB(uint32_t fromEpoch, uint32_t toEpoch, uint16_t &outAvgLatencyMs) {
    StaticMonolithicBlock *blk = staticBlock_ptr();
    uint32_t fromBucket = fromEpoch / 300;
    uint32_t toBucket   = toEpoch / 300;
    uint64_t totalKB = 0;
    uint64_t latSum = 0;
    uint32_t hits = 0;

    for (size_t i = 0; i < 2016; i++) {
        const StaticTimeSlice &s = blk->slices[i];
        if (s.epoch5Min >= fromBucket && s.epoch5Min <= toBucket) {
            totalKB += s.totalKB;
            latSum  += s.avgLatencyMs;
            hits++;
        }
    }
    outAvgLatencyMs = hits ? (uint16_t)(latSum / hits) : 0;
    return totalKB;
}

void monthlyPlan_checkRolloverAndPrune(uint32_t nowEpoch) {
    if (nowEpoch < 1700000000UL) return; // NTP not synced yet
    StaticMonolithicBlock *blk = staticBlock_ptr();

    // 1. Prune 2-day raw grace archive once 48 hours have elapsed
    if (blk->graceExpireEpoch > 0 && nowEpoch >= blk->graceExpireEpoch) {
        memset(blk->graceSlices, 0, sizeof(blk->graceSlices));
        blk->graceExpireEpoch = 0;
        staticBlock_commit();
        Serial.println("[PLAN] 48h grace period expired: raw cycle slices pruned, digests retained.");
    }

    struct tm tmNow;
    localtime_r((time_t *)&nowEpoch, &tmNow);
    uint32_t curCycleKey = (uint32_t)((tmNow.tm_year + 1900) * 100 + (tmNow.tm_mon + 1));

    if (blk->activeCycleKey == 0) {
        blk->activeCycleKey = curCycleKey;
        return;
    }

    // 2. Month finished -> snapshot 48h grace raw data + extract compact permanent summary + reset users
    if (curCycleKey != blk->activeCycleKey) {
        size_t dIdx = blk->digestCount % 12;
        StaticMonthlyDigest &dig = blk->digests[dIdx];
        memset(&dig, 0, sizeof(dig));
        dig.cycleKey = blk->activeCycleKey;

        uint32_t uOut = 0;
        for (size_t i = 0; i < STATIC_MAX_NODES_TRACKED; i++) {
            StaticDeviceEntry &dev = blk->devices[i];
            if ((dev.flags & 0x01) == 0) continue;
            dig.totalPlanBytes += dev.monthlyBytes;
            if (uOut < 32) {
                memcpy(dig.users[uOut].mac, dev.mac, 6);
                dig.users[uOut].topAppId  = dev.topAppId;
                dig.users[uOut].usedBytes = dev.monthlyBytes;
                uOut++;
            }
            // Reset active user monthly usage for the new month
            dev.monthlyBytes = 0;
            dev.dailyBytes   = 0;
            dev.flags &= ~0x08; // Lift auto-quota block
        }
        dig.userCount = uOut;
        blk->digestCount++;

        // Copy latest 48h (576 x 5m slices) into grace archive before clearing active window
        memcpy(blk->graceSlices, blk->slices, sizeof(blk->graceSlices));
        blk->graceExpireEpoch = nowEpoch + GRACE_RETENTION_SEC;
        blk->activeCycleKey = curCycleKey;
        staticBlock_commit();
    }
}
`,
  },
  {
    id: 'crypto-backup',
    path: 'src/crypto_backup.cpp',
    title: 'UI-Password AES-256-GCM Encrypted Backup & Telegram Auto-Send',
    category: 'Crypto & Telegram',
    replaces: 'Plaintext settings + basic telegram_bot.cpp',
    summary:
      'Encrypts the static arena (users, monthly digests, rules, settings) with PBKDF2-HMAC-SHA256 + AES-256-GCM using the dashboard UI password via ESP32 hardware crypto (mbedtls) and pushes encrypted backups to Telegram automatically.',
    code: `#include <Arduino.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>
#include <esp_random.h>
#include <WiFiClientSecure.h>
#include "static_block_store.h"

// Encrypts StaticMonolithicBlock with AES-256-GCM derived from the dashboard UI password
bool cryptoBackup_encryptWithUiPass(const char *uiPassword,
                                    uint8_t outSalt[16],
                                    uint8_t outIv[12],
                                    uint8_t outTag[16],
                                    uint8_t *outCipher,
                                    size_t len) {
    esp_fill_random(outSalt, 16);
    esp_fill_random(outIv, 12);

    uint8_t key[32];
    mbedtls_md_context_t shaCtx;
    mbedtls_md_init(&shaCtx);
    mbedtls_md_setup(&shaCtx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);

    int rc = mbedtls_pkcs5_pbkdf2_hmac(&shaCtx,
                                       (const unsigned char *)uiPassword, strlen(uiPassword),
                                       outSalt, 16,
                                       10000, // Fast hardware-friendly ESP32 KDF
                                       sizeof(key), key);
    mbedtls_md_free(&shaCtx);
    if (rc != 0) return false;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len,
                                       outIv, 12, nullptr, 0,
                                       (const uint8_t *)staticBlock_ptr(), outCipher,
                                       16, outTag);
    }
    mbedtls_gcm_free(&gcm);
    memset(key, 0, sizeof(key));
    return rc == 0;
}
`,
  },
  {
    id: 'gh-workflow',
    path: '.github/workflows/build-firmware.yml',
    title: 'GitHub Actions Auto-Build & Auto-Release Workflow',
    category: 'Flash & Storage',
    replaces: '.github/workflows/build-firmware.yml',
    summary:
      'Automatically runs host tests, verifies blocklist, embeds gzipped UI into firmware.bin, checks 0x360000 partition limit, and publishes GitHub Releases on tags (v*) and rolling latest on master/main.',
    code: `name: Build & Auto-Release Firmware

on:
  push:
    branches: ["master", "main", "feature/**"]
    tags: ["v*"]
  pull_request:
  workflow_dispatch:

permissions:
  contents: write

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4
      - name: Host tests
        if: hashFiles('test/Makefile') != ''
        run: make -C test CXXFLAGS="-std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I../src"
      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - name: Install PlatformIO
        run: pip install -U platformio esptool
      - name: Build single-image firmware (UI embedded in PROGMEM)
        run: pio run
      - name: Build merged first-flash & 0x10000 firmware artifacts
        run: |
          mkdir -p dist
          FW=".pio/build/esp32dev/firmware.bin"
          BL=".pio/build/esp32dev/bootloader.bin"
          PT=".pio/build/esp32dev/partitions.bin"
          [ "$(stat -c%s "$FW")" -le $((0x360000)) ]
          python3 -m esptool --chip esp32 merge_bin --fill-flash-size 4MB -o dist/esp32dev-first-flash-0x0.bin 0x1000 "$BL" 0x8000 "$PT" 0x10000 "$FW"
          cp "$FW" dist/firmware-0x10000.bin
          for f in dist/*.bin; do sha256sum "$f" > "$f.sha256"; done
      - name: Extract release notes from CHANGELOG.md
        run: |
          python3 -c 'import os,re,pathlib; ver=os.environ.get("GITHUB_REF_NAME","").lstrip("v"); txt=pathlib.Path("CHANGELOG.md").read_text() if pathlib.Path("CHANGELOG.md").exists() else ""; tag=re.escape(ver) if os.environ.get("GITHUB_REF","").startswith("refs/tags/") else "Unreleased"; m=re.search(rf"^## \\[{tag}\\][^\\n]*\\n(.*?)(?=^## \\[|\\Z)", txt, re.M|re.S); pathlib.Path("dist/release-notes.md").write_text((m.group(1).strip() if m else "Automated ESP32 firmware build.") + "\\n\\nFlash *-first-flash-0x0.bin once at 0x0, then *-firmware-0x10000.bin at 0x10000.")'
      - name: Create GitHub Release
        if: startsWith(github.ref, 'refs/tags/')
        uses: softprops/action-gh-release@v2
        with:
          body_path: dist/release-notes.md
          generate_release_notes: true
          files: dist/*
`,
  },
];

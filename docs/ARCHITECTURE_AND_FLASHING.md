# Architecture, Partition Table & Flashing Reference

## 1. Unified Flash Layout (`partitions.csv`)

Replacing the dual-OTA `app0`/`app1` split (`otadata` + 2× 1.75 MB slots) and `spiffs` frees contiguous flash for one large unified `factory` partition plus a dedicated 512 KB static block:

```text
0x001000  bootloader.bin        (flashed once via first-flash-0x0.bin)
0x008000  partitions.bin        (flashed once via first-flash-0x0.bin)
0x009000  nvs       (0x007000)  WiFi RF calibration + NVS keys
0x010000  factory   (0x360000)  Firmware + embedded gzipped Web UI (.rodata PROGMEM)
0x370000  nvblock   (0x080000)  Monolithic wear-leveled StaticMonolithicBlock arena
0x3F0000  coredump  (0x010000)  Crash dump diagnostics
```

### Flashing Commands (`esptool`)

**First time only** (when changing partition table from dual-OTA/SPIFFS):
```bash
esptool.py --chip esp32 --baud 921600 write_flash 0x0 esp32-wifi-repeater-v2.0.0-first-flash-0x0.bin
```

**Every update afterward** (preserves bootloader, partitions, and `nvblock` analytics):
```bash
esptool.py --chip esp32 --baud 921600 write_flash 0x10000 esp32-wifi-repeater-v2.0.0-firmware-0x10000.bin
```

> **Trade-off**: Because `app1` is removed to fit the monolithic firmware + 512 KB `nvblock`, wireless OTA dual-slot rollback is unavailable; firmware upgrades use `0x10000` serial flashing (or CI-built `.bin` artifacts).

---

## 2. Monolithic Static Arena (`src/static_block_store.h/.cpp`)

Instead of fixed 16-user preference slots or fragmented heap `std::vector`/`String` allocations:
- A single BSS struct `StaticMonolithicBlock` holds:
  - `devices[128]`: O(1) FNV-1a open-addressing MAC table (`StaticDeviceEntry`).
  - `slices[2016]`: 7 days of 5-minute traffic & latency samples.
  - `graceSlices[576]`: 48-hour post-rollover raw retention buffer (zeroed automatically once 48h expire).
  - `digests[12]`: Permanent monthly analytics (`cycleKey`, `totalPlanBytes`, per-user `usedBytes` + `topAppId`).
- Snapshots rotate across 4 KB-aligned pages inside the 512 KB `nvblock` partition with `esp_crc32_le` integrity verification.

---

## 3. Post-Login RAM & Watchdog Stability (`src/ram_wdt_guard.cpp`)

Why the original firmware could trigger a Task Watchdog Reset (`WDT`) after login:
1. Loading the dashboard previously fired ~8 concurrent HTTP + WebSocket requests against `async_tcp`.
2. Building multiple dynamic `ArduinoJson` `JsonDocument` + `String` copies fragmented the ~110 KB contiguous DRAM heap.

Mitigations in `v2.0`:
- **Gzipped `PROGMEM` UI**: `scripts/embed_ui.py` compiles HTML assets into `.rodata`, eliminating SPIFFS file reads on login.
- **Coalesced `/api/snapshot`**: Streams JSON chunks incrementally via `AsyncResponseStream` guarded by a mutex + `esp_task_wdt_reset()` every 16 devices.
- **Non-Resetting Fault Buffer**: `RTC_NOINIT_ATTR rtcFaultLog` records subsystem warnings across soft recoveries without forcing hardware panics (`trigger_panic = false`).

---

## 4. Encrypted Backup Envelope (`.vortaxbak`)

Client-side WebCrypto implementation (`src/utils/cryptoBackup.ts`) & ESP32 `mbedtls` parity (`src/crypto_backup.cpp`):
- **Key Derivation**: `PBKDF2-HMAC-SHA256` with random 16-byte salt (100,000 iterations in browser WebCrypto / 10,000 in ESP32 `mbedtls`).
- **Cipher**: `AES-256-GCM` with random 12-byte IV.
- **Payload**: JSON snapshot containing `plan`, `devices`, `monthlySummaries`, `graceArchives`, `features`, and `timeSlices`.
- **Telegram Auto-Backup**: Dispatches text summaries + `.vortaxbak` attachments via `https://api.telegram.org` without ever persisting plaintext bot tokens into exported snapshots.

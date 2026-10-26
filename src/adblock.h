#pragma once
// Ad / tracker / site blocking by DNS. Pure C++ core (host-tested: make -C test) plus an
// Arduino part (file, flash flag, counters) at the bottom.
//
// Three lists, checked in this order, each matching a name and everything under it:
//   1. Allow   (your hosts file, lines "@@example.com")      -> always let through
//   2. Block   (your hosts file, "0.0.0.0 example.com")      -> blocked
//   3. Built-in ad/tracker list (src/blocklist_data.h, in flash, switchable) -> blocked
// A blocked name is answered here with 0.0.0.0 (nothing is forwarded), so the app gives
// up at once and no data is spent on it.

#include <stdint.h>
#include <stddef.h>

#ifndef MAX_USER_BLOCKS
  #define MAX_USER_BLOCKS 1500
#endif
#ifndef MAX_USER_ALLOWS
  #define MAX_USER_ALLOWS 100
#endif
#ifndef HOSTS_FILE_MAX
  #define HOSTS_FILE_MAX  12288
#endif

enum AdVerdict : uint8_t { AB_PASS = 0, AB_USER = 1, AB_LIST = 2, AB_ADULT = 3, AB_APP = 4 };

uint64_t adblock_hash64(const char *s, size_t n);               // FNV-1a over the lower-case name

// ---- built-in list: Golomb-Rice coded set of name hashes (see scripts/gen_blocklist.py) ----
bool     blocklist_contains(const uint8_t *data, size_t len, uint64_t hash64);
uint32_t blocklist_count(const uint8_t *data, size_t len);      // names stored, 0 if the data is bad

// ---- your hosts file ----
struct UserLists {
    uint32_t *block = nullptr;  uint16_t nBlock = 0;            // sorted 32-bit name hashes
    uint32_t *allow = nullptr;  uint16_t nAllow = 0;
};
struct HostsStats { uint16_t blocks = 0, allows = 0, ignored = 0; bool full = false; };

// Understands: "0.0.0.0 name [name...]", "127.0.0.1 name", a bare "name", "||name^" (Adblock),
// "@@name" / "@@||name^" (allow), and "#" comments. Allocates the arrays with malloc.
bool hosts_parse(const char *text, size_t len, UserLists &out, HostsStats &st);
void hosts_free(UserLists &l);

// Rewrites the text so that `domain` has exactly one rule: mode 'b' (block), 'a' (allow) or
// 'r' (none). Returns the new text length, or 0 when it does not fit in `cap`.
size_t hosts_setRule(const char *text, size_t len, const char *domain, char mode, char *out, size_t cap);

bool adblock_validHost(const char *name);                       // letters, digits, '-', '_', '.', at least one dot

// The adult filter: a second built-in list (src/adult_data.h) plus a few keywords / domain endings
// that catch the long tail. Once `locked`, the Allow list cannot lift it.
struct AdultCfg { bool on; bool locked; const uint8_t *data; size_t len; };
bool adult_match(const char *domain, const uint8_t *data, size_t len);

// Is this name blocked? listOn switches the built-in list.
AdVerdict adblock_verdict(const char *domain, const UserLists &u, bool listOn, const uint8_t *data, size_t len,
                          const AdultCfg *adult = nullptr);

// ---- Arduino part ----
#ifdef ARDUINO
#include <Arduino.h>

struct BlockedEntry { char domain[64]; uint32_t count; uint32_t clientIp; uint8_t verdict; };
struct AdblockInfo {
    bool     enabled;
    uint32_t listNames;       // names in the built-in list
    uint16_t userBlocks, userAllows;
    uint32_t blockedTotal;    // since the last restart
    uint32_t fileBytes;
    uint32_t adultNames;      // names in the adult list
    bool     adultOn, adultLocked;
};

void     adblock_init();                           // loads the flag and the hosts file
bool     adblock_enabled();
void     adblock_setEnabled(bool on);
AdVerdict adblock_check(const char *domain);        // safe from any task
void     adblock_noteBlocked(uint32_t clientIp, const char *domain, AdVerdict v);
void     adblock_info(AdblockInfo &out);
int      adblock_recent(BlockedEntry *out, int maxCount);   // most blocked first
uint32_t adblock_blockedFor(uint32_t clientIp);
String   adblock_readFile();
uint32_t adblock_nvsFree();                         // bytes of NVS still free
bool     adblock_saveFile(const String &text, String &err, HostsStats &st);
bool     adblock_setAdult(bool on, bool lock, String &err);   // a locked filter cannot be turned off
bool     adblock_adultOn();
bool     adblock_setRule(const char *domain, char mode, String &err);   // 'b', 'a' or 'r'
#endif

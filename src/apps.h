#pragma once
// App recognition. Pure C++ (no Arduino headers) so it can be unit-tested on a PC:
//   make -C test
//
// How an app is recognised
//   1. DNS: the repeater is every device's DNS server, so it sees "who asked for
//      which name". The name is matched against a suffix table (youtube.com,
//      googlevideo.com ...). CNAME targets are matched too, which catches CDN names.
//   2. The answer's IPv4 addresses are remembered (IP -> app), so the bytes that later
//      flow to that IP are counted for the app.
//   3. A few apps (Telegram) skip DNS and connect to fixed IP ranges; those are matched
//      by range.
// What it cannot see: apps that hide their lookups (DNS over HTTPS, a VPN) and traffic
// to IPs that were never looked up. That traffic is counted under "Other".

#include <stdint.h>
#include <stddef.h>
#ifdef ARDUINO
  #include "config.h"
#endif
#ifndef MAX_CUSTOM_RULES
  #define MAX_CUSTOM_RULES 24
#endif

// id, display name, category, generic
// "generic" apps share front-end IPs with many others (Google), so they must not
// overwrite a more specific app that was just seen on the same address.
#define APP_LIST(X) \
  X(OTHER,        "Other",              CAT_OTHER,     1) \
  X(YOUTUBE,      "YouTube",            CAT_VIDEO,     0) \
  X(NETFLIX,      "Netflix",            CAT_VIDEO,     0) \
  X(TIKTOK,       "TikTok",             CAT_VIDEO,     0) \
  X(SHAHID,       "Shahid",             CAT_VIDEO,     0) \
  X(DISNEY,       "Disney+ / Hotstar",  CAT_VIDEO,     0) \
  X(PRIMEVIDEO,   "Prime Video",        CAT_VIDEO,     0) \
  X(TWITCH,       "Twitch",             CAT_VIDEO,     0) \
  X(WATCHIT,      "Watch it",           CAT_VIDEO,     0) \
  X(OTHERVIDEO,   "Other video",        CAT_VIDEO,     0) \
  X(FACEBOOK,     "Facebook",           CAT_SOCIAL,    0) \
  X(INSTAGRAM,    "Instagram",          CAT_SOCIAL,    0) \
  X(SNAPCHAT,     "Snapchat",           CAT_SOCIAL,    0) \
  X(XTWITTER,     "X (Twitter)",        CAT_SOCIAL,    0) \
  X(REDDIT,       "Reddit",             CAT_SOCIAL,    0) \
  X(PINTEREST,    "Pinterest",          CAT_SOCIAL,    0) \
  X(LINKEDIN,     "LinkedIn",           CAT_SOCIAL,    0) \
  X(WHATSAPP,     "WhatsApp",           CAT_MESSAGING, 0) \
  X(TELEGRAM,     "Telegram",           CAT_MESSAGING, 0) \
  X(MESSENGER,    "Messenger",          CAT_MESSAGING, 0) \
  X(DISCORD,      "Discord",            CAT_MESSAGING, 0) \
  X(SIGNAL,       "Signal",             CAT_MESSAGING, 0) \
  X(VIBER,        "Viber",              CAT_MESSAGING, 0) \
  X(ZOOM,         "Zoom",               CAT_MESSAGING, 0) \
  X(TEAMS,        "Teams / Skype",      CAT_MESSAGING, 0) \
  X(MEET,         "Google Meet",        CAT_MESSAGING, 0) \
  X(GOOGLE,       "Google",             CAT_BROWSING,  1) \
  X(SEARCH,       "Bing / Yahoo / DDG", CAT_BROWSING,  0) \
  X(WIKIPEDIA,    "Wikipedia",          CAT_BROWSING,  0) \
  X(AI,           "AI assistants",      CAT_BROWSING,  0) \
  X(SPOTIFY,      "Spotify",            CAT_MUSIC,     0) \
  X(ANGHAMI,      "Anghami",            CAT_MUSIC,     0) \
  X(YTMUSIC,      "YouTube Music",      CAT_MUSIC,     0) \
  X(APPLEMUSIC,   "Apple Music",        CAT_MUSIC,     0) \
  X(OTHERMUSIC,   "Other music",        CAT_MUSIC,     0) \
  X(STEAM,        "Steam",              CAT_GAMING,    0) \
  X(EPIC,         "Epic Games",         CAT_GAMING,    0) \
  X(PUBG,         "PUBG Mobile",        CAT_GAMING,    0) \
  X(FREEFIRE,     "Free Fire",          CAT_GAMING,    0) \
  X(COD,          "Call of Duty",       CAT_GAMING,    0) \
  X(ROBLOX,       "Roblox",             CAT_GAMING,    0) \
  X(MINECRAFT,    "Minecraft",          CAT_GAMING,    0) \
  X(CONSOLES,     "Xbox / PlayStation", CAT_GAMING,    0) \
  X(SUPERCELL,    "Supercell",          CAT_GAMING,    0) \
  X(EA,           "EA",                 CAT_GAMING,    0) \
  X(RIOT,         "Riot Games",         CAT_GAMING,    0) \
  X(BLIZZARD,     "Blizzard",           CAT_GAMING,    0) \
  X(HOYOVERSE,    "HoYoverse",          CAT_GAMING,    0) \
  X(OTHERGAMES,   "Other games",        CAT_GAMING,    0) \
  X(PLAYSTORE,    "Google Play",        CAT_DOWNLOADS, 0) \
  X(APPSTORE,     "App Store",          CAT_DOWNLOADS, 0) \
  X(WINUPDATE,    "Windows Update",     CAT_DOWNLOADS, 0) \
  X(APPLEUPDATE,  "Apple updates",      CAT_DOWNLOADS, 0) \
  X(PHONEUPDATE,  "Phone updates",      CAT_DOWNLOADS, 0) \
  X(CLOUD,        "Cloud storage",      CAT_DOWNLOADS, 0) \
  X(SYSTEM,       "System & time",      CAT_OTHER,     0) \
  X(ADS,          "Ads & trackers",     CAT_OTHER,     0)

enum AppCategory : uint8_t {
    CAT_VIDEO = 0, CAT_SOCIAL, CAT_MESSAGING, CAT_BROWSING,
    CAT_MUSIC, CAT_GAMING, CAT_DOWNLOADS, CAT_OTHER,
    CAT_COUNT
};

enum AppId : uint8_t {
#define X(id, name, cat, generic) APP_##id,
    APP_LIST(X)
#undef X
    APP_COUNT
};
static_assert(APP_COUNT <= 64, "per-device app counters are sized for at most 64 apps");

const char *apps_name(uint8_t app);
const char *apps_catName(uint8_t cat);
uint8_t     apps_category(uint8_t app);
bool        apps_isGeneric(uint8_t app);

// Which app does this hostname belong to? APP_OTHER when nobody claims it.
// Custom rules (added from the dashboard) win over the built-in table.
uint8_t apps_classifyDomain(const char *domain);

// ---- custom rules (the user teaches the repeater a domain) ----
struct AppRule { char suffix[41]; uint8_t app; };
bool apps_customAdd(const char *suffix, uint8_t app);      // false: invalid or table full
bool apps_customRemove(const char *suffix);
int  apps_customList(AppRule *out, int maxCount);
bool apps_validSuffix(const char *suffix);                 // lower-case letters, digits, '.', '-'; has a dot

// ---- IP -> app memory (filled from DNS answers, read for every frame) ----
// nowMin: any clock in minutes (millis()/60000). Entries older than 6 h are ignored.
void    apps_cachePut(uint32_t ip, uint8_t app, uint16_t nowMin);
uint8_t apps_lookupIp(uint32_t ip, uint16_t nowMin);       // APP_OTHER when unknown
void    apps_cacheClear();                                  // tests

#ifdef ARDUINO
void apps_loadCustom();        // restore custom rules from flash
#endif

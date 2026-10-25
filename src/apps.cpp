#include "apps.h"
#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Preferences.h>
  #include "config.h"
  static portMUX_TYPE appsMux = portMUX_INITIALIZER_UNLOCKED;
  #define APPS_LOCK()   portENTER_CRITICAL(&appsMux)
  #define APPS_UNLOCK() portEXIT_CRITICAL(&appsMux)
#else
  #define APPS_LOCK()
  #define APPS_UNLOCK()
#endif

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------

static const char *const APP_NAMES[] = {
#define X(id, name, cat, generic) name,
    APP_LIST(X)
#undef X
};
static const uint8_t APP_CATS[] = {
#define X(id, name, cat, generic) cat,
    APP_LIST(X)
#undef X
};
static const uint8_t APP_GENERIC[] = {
#define X(id, name, cat, generic) generic,
    APP_LIST(X)
#undef X
};
static const char *const CAT_NAMES[] = {
    "Video streaming", "Social media", "Messaging & calls", "Web & search",
    "Music", "Gaming", "Downloads & updates", "Other",
};

const char *apps_name(uint8_t app)    { return app < APP_COUNT ? APP_NAMES[app] : "Other"; }
const char *apps_catName(uint8_t cat) { return cat < CAT_COUNT ? CAT_NAMES[cat] : "Other"; }
uint8_t     apps_category(uint8_t app){ return app < APP_COUNT ? APP_CATS[app] : (uint8_t)CAT_OTHER; }
bool        apps_isGeneric(uint8_t app){ return app < APP_COUNT ? APP_GENERIC[app] != 0 : true; }

// ---------------------------------------------------------------------------
// The built-in suffix table.
// A rule matches a name that equals the suffix or ends with ".suffix"; when
// several match, the LONGEST suffix wins (music.youtube.com beats youtube.com).
//
// Sources: the apps' own network-requirements pages where they exist (WhatsApp,
// Meta, Telegram's published ranges), public block/allow lists, and common
// knowledge. Anything wrong or missing can be fixed without a new firmware:
// open Apps > "Domains I don't know yet" in the dashboard and teach the
// repeater a domain, or send me the list.
// ---------------------------------------------------------------------------

struct Rule { const char *suffix; uint8_t app; };

#define R(s, a) { s, APP_##a },
static const Rule RULES[] = {
    // ---- YouTube / Google video ----
    R("youtube.com", YOUTUBE) R("youtu.be", YOUTUBE) R("googlevideo.com", YOUTUBE)
    R("ytimg.com", YOUTUBE) R("youtube-nocookie.com", YOUTUBE) R("youtubei.googleapis.com", YOUTUBE)
    R("youtube.googleapis.com", YOUTUBE) R("yt3.ggpht.com", YOUTUBE) R("youtubekids.com", YOUTUBE)
    R("withyoutube.com", YOUTUBE) R("ytimg.l.google.com", YOUTUBE) R("yt.be", YOUTUBE)
    R("music.youtube.com", YTMUSIC)

    // ---- TikTok (ByteDance) ----
    R("tiktok.com", TIKTOK) R("tiktokv.com", TIKTOK) R("tiktokcdn.com", TIKTOK)
    R("tiktokcdn-us.com", TIKTOK) R("tiktokcdn-eu.com", TIKTOK) R("tiktokv.us", TIKTOK)
    R("tiktokw.us", TIKTOK) R("byteoversea.com", TIKTOK) R("ibytedtos.com", TIKTOK)
    R("ibyteimg.com", TIKTOK) R("ttwstatic.com", TIKTOK) R("muscdn.com", TIKTOK)
    R("musical.ly", TIKTOK) R("bytefcdn-oversea.com", TIKTOK) R("ttlivecdn.com", TIKTOK)
    R("tiktokrow-cdn.com", TIKTOK) R("tiktok-cdn.com", TIKTOK)

    // ---- Netflix ----
    R("netflix.com", NETFLIX) R("netflix.net", NETFLIX) R("nflxvideo.net", NETFLIX)
    R("nflximg.net", NETFLIX) R("nflximg.com", NETFLIX) R("nflxso.net", NETFLIX)
    R("nflxext.com", NETFLIX) R("nflxsearch.net", NETFLIX)

    // ---- Other video ----
    R("shahid.net", SHAHID) R("shahid.mbc.net", SHAHID) R("mbc.net", SHAHID) R("shahid.com", SHAHID)
    R("disneyplus.com", DISNEY) R("disney-plus.net", DISNEY) R("bamgrid.com", DISNEY)
    R("dssott.com", DISNEY) R("disneystreaming.com", DISNEY) R("hotstar.com", DISNEY)
    R("hotstarext.com", DISNEY)
    R("primevideo.com", PRIMEVIDEO) R("aiv-cdn.net", PRIMEVIDEO) R("pv-cdn.net", PRIMEVIDEO)
    R("aiv-delivery.net", PRIMEVIDEO) R("amazonvideo.com", PRIMEVIDEO)
    R("twitch.tv", TWITCH) R("ttvnw.net", TWITCH) R("jtvnw.net", TWITCH) R("twitchcdn.net", TWITCH)
    R("twitchsvc.net", TWITCH) R("live-video.net", TWITCH)
    R("watchit.com", WATCHIT) R("watchit.tv", WATCHIT)
    R("vimeo.com", OTHERVIDEO) R("vimeocdn.com", OTHERVIDEO) R("dailymotion.com", OTHERVIDEO)
    R("dmcdn.net", OTHERVIDEO) R("kick.com", OTHERVIDEO) R("starzplay.com", OTHERVIDEO)
    R("osnplus.com", OTHERVIDEO) R("tod.tv", OTHERVIDEO) R("viu.com", OTHERVIDEO)
    R("crunchyroll.com", OTHERVIDEO) R("iq.com", OTHERVIDEO) R("iqiyi.com", OTHERVIDEO)
    R("wetv.vip", OTHERVIDEO) R("jwplayer.com", OTHERVIDEO) R("jwpcdn.com", OTHERVIDEO)

    // ---- Social ----
    R("facebook.com", FACEBOOK) R("fb.com", FACEBOOK) R("fbcdn.net", FACEBOOK)
    R("facebook.net", FACEBOOK) R("fbsbx.com", FACEBOOK) R("fb.me", FACEBOOK)
    R("fbpigeon.com", FACEBOOK)
    R("messenger.com", MESSENGER) R("m.me", MESSENGER)
    R("instagram.com", INSTAGRAM) R("cdninstagram.com", INSTAGRAM) R("instagr.am", INSTAGRAM)
    R("ig.me", INSTAGRAM) R("threads.net", INSTAGRAM) R("threads.com", INSTAGRAM)
    R("snapchat.com", SNAPCHAT) R("sc-cdn.net", SNAPCHAT) R("snap-dev.net", SNAPCHAT)
    R("snapkit.com", SNAPCHAT) R("feelinsonice.com", SNAPCHAT) R("sc-gw.com", SNAPCHAT)
    R("snapads.com", SNAPCHAT) R("sc-static.net", SNAPCHAT) R("bitmoji.com", SNAPCHAT)
    R("bitstrips.com", SNAPCHAT) R("snap.com", SNAPCHAT) R("sc-jpl.com", SNAPCHAT)
    R("twitter.com", XTWITTER) R("x.com", XTWITTER) R("twimg.com", XTWITTER) R("t.co", XTWITTER)
    R("twttr.com", XTWITTER) R("periscope.tv", XTWITTER) R("pscp.tv", XTWITTER)
    R("reddit.com", REDDIT) R("redd.it", REDDIT) R("redditmedia.com", REDDIT)
    R("redditstatic.com", REDDIT)
    R("pinterest.com", PINTEREST) R("pinimg.com", PINTEREST) R("pin.it", PINTEREST)
    R("linkedin.com", LINKEDIN) R("licdn.com", LINKEDIN)

    // ---- Messaging & calls ----
    R("whatsapp.com", WHATSAPP) R("whatsapp.net", WHATSAPP) R("wa.me", WHATSAPP)
    R("telegram.org", TELEGRAM) R("t.me", TELEGRAM) R("telegram.me", TELEGRAM)
    R("telegra.ph", TELEGRAM) R("cdn-telegram.org", TELEGRAM) R("tdesktop.com", TELEGRAM)
    R("telesco.pe", TELEGRAM) R("tg.dev", TELEGRAM) R("telegram-cdn.org", TELEGRAM)
    R("discord.com", DISCORD) R("discordapp.com", DISCORD) R("discordapp.net", DISCORD)
    R("discord.gg", DISCORD) R("discord.media", DISCORD) R("discordcdn.com", DISCORD)
    R("signal.org", SIGNAL) R("whispersystems.org", SIGNAL) R("signal.art", SIGNAL)
    R("viber.com", VIBER) R("vibercdn.com", VIBER)
    R("zoom.us", ZOOM) R("zoom.com", ZOOM) R("zoomgov.com", ZOOM)
    R("teams.microsoft.com", TEAMS) R("teams.live.com", TEAMS) R("teams.cdn.office.net", TEAMS)
    R("skype.com", TEAMS) R("skypeassets.com", TEAMS) R("lync.com", TEAMS)
    R("meet.google.com", MEET) R("meet.googleapis.com", MEET)

    // ---- Google (generic) ----
    R("google.com", GOOGLE) R("google.com.eg", GOOGLE) R("gstatic.com", GOOGLE)
    R("googleapis.com", GOOGLE) R("googleusercontent.com", GOOGLE) R("gmail.com", GOOGLE)
    R("ggpht.com", GOOGLE) R("googletagmanager.com", GOOGLE) R("google-analytics.com", GOOGLE)
    R("doubleclick.net", GOOGLE) R("googlesyndication.com", GOOGLE)
    R("googleadservices.com", GOOGLE) R("gvt2.com", GOOGLE) R("app-measurement.com", GOOGLE)

    // ---- Search, wiki, AI ----
    R("bing.com", SEARCH) R("bing.net", SEARCH) R("duckduckgo.com", SEARCH) R("yahoo.com", SEARCH)
    R("yimg.com", SEARCH) R("ecosia.org", SEARCH) R("yandex.com", SEARCH) R("yandex.ru", SEARCH)
    R("wikipedia.org", WIKIPEDIA) R("wikimedia.org", WIKIPEDIA) R("wikidata.org", WIKIPEDIA)
    R("openai.com", AI) R("chatgpt.com", AI) R("oaistatic.com", AI) R("oaiusercontent.com", AI)
    R("claude.ai", AI) R("anthropic.com", AI) R("gemini.google.com", AI) R("bard.google.com", AI)
    R("perplexity.ai", AI) R("deepseek.com", AI) R("grok.com", AI) R("copilot.microsoft.com", AI)
    R("poe.com", AI) R("character.ai", AI)

    // ---- Music ----
    R("spotify.com", SPOTIFY) R("scdn.co", SPOTIFY) R("spotifycdn.com", SPOTIFY)
    R("spoti.fi", SPOTIFY) R("pscdn.co", SPOTIFY) R("spotilocal.com", SPOTIFY)
    R("anghami.com", ANGHAMI) R("anghamicdn.com", ANGHAMI)
    R("music.apple.com", APPLEMUSIC) R("applemusic.com", APPLEMUSIC)
    R("soundcloud.com", OTHERMUSIC) R("sndcdn.com", OTHERMUSIC) R("deezer.com", OTHERMUSIC)
    R("dzcdn.net", OTHERMUSIC) R("tidal.com", OTHERMUSIC) R("tunein.com", OTHERMUSIC)
    R("audiomack.com", OTHERMUSIC) R("napster.com", OTHERMUSIC)

    // ---- Games ----
    R("steampowered.com", STEAM) R("steamcontent.com", STEAM) R("steamstatic.com", STEAM)
    R("steamgames.com", STEAM) R("steamcommunity.com", STEAM) R("steamserver.net", STEAM)
    R("steamusercontent.com", STEAM) R("valvesoftware.com", STEAM)
    R("epicgames.com", EPIC) R("unrealengine.com", EPIC) R("epicgames.dev", EPIC)
    R("easyanticheat.net", EPIC) R("fortnite.com", EPIC)
    R("pubgmobile.com", PUBG) R("igamecj.com", PUBG) R("proximabeta.com", PUBG)
    R("pubg.com", PUBG) R("krafton.com", PUBG) R("gpubgm.com", PUBG) R("intlgame.com", PUBG)
    R("freefiremobile.com", FREEFIRE) R("garena.com", FREEFIRE) R("garenanow.com", FREEFIRE)
    R("callofduty.com", COD) R("activision.com", COD) R("demonware.net", COD)
    R("roblox.com", ROBLOX) R("rbxcdn.com", ROBLOX) R("robloxlabs.com", ROBLOX) R("rbx.com", ROBLOX)
    R("minecraft.net", MINECRAFT) R("mojang.com", MINECRAFT) R("minecraftservices.com", MINECRAFT)
    R("xboxlive.com", CONSOLES) R("xbox.com", CONSOLES) R("xboxservices.com", CONSOLES)
    R("playstation.com", CONSOLES) R("playstation.net", CONSOLES)
    R("sonyentertainmentnetwork.com", CONSOLES) R("nintendo.com", CONSOLES) R("nintendo.net", CONSOLES)
    R("supercell.com", SUPERCELL) R("supercell.net", SUPERCELL) R("supercellid.com", SUPERCELL)
    R("clashofclans.com", SUPERCELL) R("clashroyale.com", SUPERCELL) R("brawlstars.com", SUPERCELL)
    R("ea.com", EA) R("easports.com", EA) R("origin.com", EA)
    R("riotgames.com", RIOT) R("leagueoflegends.com", RIOT) R("pvp.net", RIOT) R("riotcdn.net", RIOT)
    R("blizzard.com", BLIZZARD) R("battle.net", BLIZZARD)
    R("hoyoverse.com", HOYOVERSE) R("mihoyo.com", HOYOVERSE) R("hoyolab.com", HOYOVERSE)
    R("yuanshen.com", HOYOVERSE)
    R("moonton.com", OTHERGAMES) R("mobilelegends.com", OTHERGAMES) R("lilithgames.com", OTHERGAMES)
    R("king.com", OTHERGAMES) R("zynga.com", OTHERGAMES) R("playrix.com", OTHERGAMES)
    R("scopely.com", OTHERGAMES) R("nianticlabs.com", OTHERGAMES) R("gameloft.com", OTHERGAMES)
    R("ubisoft.com", OTHERGAMES) R("ubi.com", OTHERGAMES) R("rockstargames.com", OTHERGAMES)

    // ---- Downloads, updates, app stores, cloud ----
    R("play.googleapis.com", PLAYSTORE) R("play.google.com", PLAYSTORE)
    R("android.clients.google.com", PLAYSTORE) R("dl.google.com", PLAYSTORE) R("gvt1.com", PLAYSTORE)
    R("android.googleapis.com", PLAYSTORE) R("play-lh.googleusercontent.com", PLAYSTORE)
    R("update.googleapis.com", PLAYSTORE) R("googleplay.com", PLAYSTORE)
    R("apps.apple.com", APPSTORE) R("itunes.apple.com", APPSTORE) R("mzstatic.com", APPSTORE)
    R("iosapps.itunes.apple.com", APPSTORE)
    R("windowsupdate.com", WINUPDATE) R("update.microsoft.com", WINUPDATE)
    R("delivery.mp.microsoft.com", WINUPDATE) R("do.dsp.mp.microsoft.com", WINUPDATE)
    R("download.microsoft.com", WINUPDATE) R("officecdn.microsoft.com", WINUPDATE)
    R("swcdn.apple.com", APPLEUPDATE) R("swscan.apple.com", APPLEUPDATE) R("mesu.apple.com", APPLEUPDATE)
    R("appldnld.apple.com", APPLEUPDATE) R("updates.cdn-apple.com", APPLEUPDATE)
    R("swdist.apple.com", APPLEUPDATE) R("gdmf.apple.com", APPLEUPDATE)
    R("samsungapps.com", PHONEUPDATE) R("samsungcloudsolution.com", PHONEUPDATE)
    R("samsungdm.com", PHONEUPDATE) R("ospserver.net", PHONEUPDATE) R("miui.com", PHONEUPDATE)
    R("xiaomi.com", PHONEUPDATE) R("mi.com", PHONEUPDATE) R("hicloud.com", PHONEUPDATE)
    R("dbankcloud.com", PHONEUPDATE) R("heytapmobile.com", PHONEUPDATE) R("coloros.com", PHONEUPDATE)
    R("oppomobile.com", PHONEUPDATE) R("vivoglobal.com", PHONEUPDATE) R("hihonor.com", PHONEUPDATE)
    R("drive.google.com", CLOUD) R("docs.google.com", CLOUD) R("drive.usercontent.google.com", CLOUD)
    R("dropbox.com", CLOUD) R("dropboxstatic.com", CLOUD) R("dropboxapi.com", CLOUD)
    R("onedrive.com", CLOUD) R("onedrive.live.com", CLOUD) R("sharepoint.com", CLOUD)
    R("icloud.com", CLOUD) R("icloud-content.com", CLOUD) R("mega.nz", CLOUD) R("mega.io", CLOUD)
    R("box.com", CLOUD) R("wetransfer.com", CLOUD) R("mediafire.com", CLOUD)
};
#undef R
static const int RULE_COUNT = (int)(sizeof(RULES) / sizeof(RULES[0]));

// Apps that skip DNS and connect to fixed address ranges.
// Telegram's published ranges (core.telegram.org/resources/cidr.txt). The first octet
// group is stored in the byte order an IPv4 header is loaded in (little-endian CPU).
struct Cidr { uint32_t net; uint32_t mask; uint8_t app; };
#define OCT(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define MASK(bits)      (__builtin_bswap32(0xFFFFFFFFu << (32 - (bits))))
static const Cidr CIDRS[] = {
    { OCT(149, 154, 160, 0), MASK(20), APP_TELEGRAM },
    { OCT(91, 108, 4, 0),    MASK(22), APP_TELEGRAM },
    { OCT(91, 108, 8, 0),    MASK(22), APP_TELEGRAM },
    { OCT(91, 108, 12, 0),   MASK(22), APP_TELEGRAM },
    { OCT(91, 108, 16, 0),   MASK(22), APP_TELEGRAM },
    { OCT(91, 108, 20, 0),   MASK(22), APP_TELEGRAM },
    { OCT(91, 108, 56, 0),   MASK(22), APP_TELEGRAM },
    { OCT(185, 76, 151, 0),  MASK(24), APP_TELEGRAM },
};
static const int CIDR_COUNT = (int)(sizeof(CIDRS) / sizeof(CIDRS[0]));

// ---------------------------------------------------------------------------
// custom rules
// ---------------------------------------------------------------------------

static AppRule customRules[MAX_CUSTOM_RULES];
static int     customCount = 0;

bool apps_validSuffix(const char *s) {
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 3 || n > 40) return false;
    bool dot = false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
        if (!ok) return false;
        if (c == '.') {
            dot = true;
            if (i == 0 || i == n - 1 || s[i - 1] == '.') return false;
        }
    }
    return dot;
}

#ifdef ARDUINO
static void saveCustom() {
    AppRule copy[MAX_CUSTOM_RULES];
    int n;
    APPS_LOCK();
    n = customCount;
    memcpy(copy, customRules, sizeof(copy));
    APPS_UNLOCK();
    Preferences p;
    p.begin("apprules", false);
    if (n > 0) p.putBytes("r", copy, n * sizeof(AppRule));
    else       p.remove("r");
    p.end();
}

void apps_loadCustom() {
    Preferences p;
    p.begin("apprules", false);
    AppRule tmp[MAX_CUSTOM_RULES];
    size_t bytes = p.getBytes("r", tmp, sizeof(tmp));
    p.end();
    int n = (int)(bytes / sizeof(AppRule));
    APPS_LOCK();
    customCount = 0;
    for (int i = 0; i < n && i < MAX_CUSTOM_RULES; i++) {
        tmp[i].suffix[sizeof(tmp[i].suffix) - 1] = '\0';
        if (apps_validSuffix(tmp[i].suffix) && tmp[i].app < APP_COUNT) customRules[customCount++] = tmp[i];
    }
    APPS_UNLOCK();
}
#else
static void saveCustom() {}
#endif

bool apps_customAdd(const char *suffix, uint8_t app) {
    if (!apps_validSuffix(suffix) || app >= APP_COUNT) return false;
    bool ok = true;
    APPS_LOCK();
    int idx = -1;
    for (int i = 0; i < customCount; i++)
        if (strcmp(customRules[i].suffix, suffix) == 0) { idx = i; break; }
    if (idx < 0) {
        if (customCount >= MAX_CUSTOM_RULES) ok = false;
        else idx = customCount++;
    }
    if (ok) {
        strncpy(customRules[idx].suffix, suffix, sizeof(customRules[idx].suffix) - 1);
        customRules[idx].suffix[sizeof(customRules[idx].suffix) - 1] = '\0';
        customRules[idx].app = app;
    }
    APPS_UNLOCK();
    if (ok) saveCustom();
    return ok;
}

bool apps_customRemove(const char *suffix) {
    bool found = false;
    APPS_LOCK();
    for (int i = 0; i < customCount; i++) {
        if (strcmp(customRules[i].suffix, suffix) == 0) {
            customRules[i] = customRules[customCount - 1];
            customCount--;
            found = true;
            break;
        }
    }
    APPS_UNLOCK();
    if (found) saveCustom();
    return found;
}

int apps_customList(AppRule *out, int maxCount) {
    int n;
    APPS_LOCK();
    n = customCount < maxCount ? customCount : maxCount;
    memcpy(out, customRules, n * sizeof(AppRule));
    APPS_UNLOCK();
    return n;
}

// ---------------------------------------------------------------------------
// domain matching
// ---------------------------------------------------------------------------

static inline bool endsWithLabel(const char *dom, size_t dlen, const char *suf, size_t slen) {
    if (slen > dlen) return false;
    if (dom[dlen - 1] != suf[slen - 1]) return false;                 // cheap early reject
    if (memcmp(dom + dlen - slen, suf, slen) != 0) return false;
    return dlen == slen || dom[dlen - slen - 1] == '.';
}

uint8_t apps_classifyDomain(const char *domain) {
    if (!domain || !*domain) return APP_OTHER;
    char d[96];
    size_t n = 0;
    for (; domain[n] && n < sizeof(d) - 1; n++) {
        char c = domain[n];
        d[n] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    while (n > 0 && d[n - 1] == '.') n--;                             // trailing dot
    d[n] = '\0';
    if (n == 0) return APP_OTHER;

    size_t bestLen = 0;
    uint8_t best = APP_OTHER;

    AppRule custom[MAX_CUSTOM_RULES];
    int cn = apps_customList(custom, MAX_CUSTOM_RULES);
    bool customHit = false;
    for (int i = 0; i < cn; i++) {
        size_t sl = strlen(custom[i].suffix);
        if (sl > bestLen && endsWithLabel(d, n, custom[i].suffix, sl)) { bestLen = sl; best = custom[i].app; customHit = true; }
    }
    for (int i = 0; i < RULE_COUNT; i++) {
        size_t sl = strlen(RULES[i].suffix);
        if (sl > bestLen && endsWithLabel(d, n, RULES[i].suffix, sl)) {
            // A custom rule of any length beats a built-in one: the user said so.
            if (customHit) continue;
            bestLen = sl; best = RULES[i].app;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// IP -> app memory: 256 sets x 2 ways, direct hashed, no allocation.
// ---------------------------------------------------------------------------

struct CacheEntry { uint32_t ip; uint16_t minute; uint8_t app; uint8_t used; };
#define CACHE_SETS 256
static CacheEntry cache[CACHE_SETS][2];
#ifdef ARDUINO
static portMUX_TYPE cacheMux = portMUX_INITIALIZER_UNLOCKED;
#define CACHE_LOCK()   portENTER_CRITICAL(&cacheMux)
#define CACHE_UNLOCK() portEXIT_CRITICAL(&cacheMux)
#else
#define CACHE_LOCK()
#define CACHE_UNLOCK()
#endif

static inline uint32_t setOf(uint32_t ip) { return (ip * 2654435761u) >> 24; }
#define CACHE_MAX_AGE_MIN 360    // 6 hours

void apps_cacheClear() {
    CACHE_LOCK();
    memset(cache, 0, sizeof(cache));
    CACHE_UNLOCK();
}

void apps_cachePut(uint32_t ip, uint8_t app, uint16_t nowMin) {
    if (ip == 0 || app == APP_OTHER || app >= APP_COUNT) return;
    CACHE_LOCK();
    CacheEntry *set = cache[setOf(ip)];
    int target = -1;
    for (int w = 0; w < 2; w++) {
        if (set[w].used && set[w].ip == ip) {
            // Do not let a generic app (Google) steal an address a specific app used a moment ago.
            uint16_t age = (uint16_t)(nowMin - set[w].minute);
            if (apps_isGeneric(app) && !apps_isGeneric(set[w].app) && age < 10) { CACHE_UNLOCK(); return; }
            target = w;
            break;
        }
    }
    if (target < 0) {
        if (!set[0].used)      target = 0;
        else if (!set[1].used) target = 1;
        else target = ((uint16_t)(nowMin - set[0].minute) >= (uint16_t)(nowMin - set[1].minute)) ? 0 : 1;   // evict the older
    }
    set[target].ip = ip; set[target].app = app; set[target].minute = nowMin; set[target].used = 1;
    CACHE_UNLOCK();
}

uint8_t apps_lookupIp(uint32_t ip, uint16_t nowMin) {
    if (ip == 0) return APP_OTHER;
    uint8_t found = APP_OTHER;
    CACHE_LOCK();
    const CacheEntry *set = cache[setOf(ip)];
    for (int w = 0; w < 2; w++) {
        if (set[w].used && set[w].ip == ip && (uint16_t)(nowMin - set[w].minute) <= CACHE_MAX_AGE_MIN) {
            found = set[w].app;
            break;
        }
    }
    CACHE_UNLOCK();
    if (found != APP_OTHER) return found;
    for (int i = 0; i < CIDR_COUNT; i++)
        if ((ip & CIDRS[i].mask) == CIDRS[i].net) return CIDRS[i].app;
    return APP_OTHER;
}

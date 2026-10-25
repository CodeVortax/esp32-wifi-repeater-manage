#include "telegram_bot.h"
#include "config.h"
#include "settings.h"
#include "quota_manager.h"
#include "diagnostics.h"
#include "wifi_repeater.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <new>

static String formatStatus() {
    // Heap, not stack: the device struct is large and this task's stack is small.
    DeviceUsage *devs = new (std::nothrow) DeviceUsage[MAX_TRACKED_DEVICES];
    if (!devs) return "Out of memory, try again.";
    int n = quota_snapshot(devs, MAX_TRACKED_DEVICES);
    if (n == 0) { delete[] devs; return "No devices seen yet."; }

    String out = "Devices:\n";
    for (int i = 0; i < n; i++) {
        char label[48];
        if (devs[i].name[0]) snprintf(label, sizeof(label), "%s", devs[i].name);
        else                 snprintf(label, sizeof(label), "User %u", devs[i].userNo);
        char line[160];
        snprintf(line, sizeof(line), "%s %s (%s)  %.0f/%s MB %s%s\n",
                 devs[i].online ? "●" : "○",
                 label,
                 devs[i].macStr,
                 devs[i].bytesToday / (1024.0 * 1024.0),
                 devs[i].dailyLimitMB ? String(devs[i].dailyLimitMB).c_str() : "unlimited",
                 devs[i].blocked ? "[BLOCKED] " : "",
                 devs[i].whitelisted ? "[WHITELIST]" : "");
        out += line;
    }
    delete[] devs;
    return out;
}

static void handleCommand(UniversalTelegramBot &bot, const String &chatId, String text) {
    text.trim();
    int sp = text.indexOf(' ');
    String cmd = (sp < 0) ? text : text.substring(0, sp);
    String arg = (sp < 0) ? ""   : text.substring(sp + 1);
    arg.trim();
    cmd.toLowerCase();
    int at = cmd.indexOf('@');                    // "/status@MyBot" in groups
    if (at > 0) cmd = cmd.substring(0, at);

    if (cmd == "/status") {
        bot.sendMessage(chatId, formatStatus(), "");
    } else if (cmd == "/block") {
        bot.sendMessage(chatId, quota_block(arg.c_str()) ? "Blocked " + arg : "Device not found", "");
    } else if (cmd == "/unblock") {
        bot.sendMessage(chatId, quota_unblock(arg.c_str()) ? "Unblocked " + arg : "Device not found", "");
    } else if (cmd == "/whitelist" || cmd == "/unwhitelist") {
        bool on = (cmd == "/whitelist");
        bot.sendMessage(chatId, quota_setWhitelist(arg.c_str(), on) ? (on ? "Whitelisted " : "Removed from whitelist ") + arg
                                                                      : "Device not found", "");
    } else if (cmd == "/setquota") {
        int sp2 = arg.indexOf(' ');
        if (sp2 < 0) { bot.sendMessage(chatId, "Usage: /setquota AA:BB:CC:DD:EE:FF 500  (MB, 0 = unlimited)", ""); return; }
        String mac = arg.substring(0, sp2);
        long mb = arg.substring(sp2 + 1).toInt();
        if (mb < 0 || mb > 10000000) { bot.sendMessage(chatId, "Quota must be 0 - 10000000 MB", ""); return; }
        bot.sendMessage(chatId, quota_setLimit(mac.c_str(), (uint32_t)mb) ? "Quota updated" : "Device not found", "");
    } else if (cmd == "/ping") {
        bot.sendMessage(chatId, diagnostics_pingBlocking(arg.length() ? arg : "8.8.8.8"), "");
    } else if (cmd == "/help" || cmd == "/start") {
        bot.sendMessage(chatId,
            "/status\n/block <mac>\n/unblock <mac>\n/whitelist <mac>\n/unwhitelist <mac>\n"
            "/setquota <mac> <mb>\n/ping <host>", "");
    } else {
        bot.sendMessage(chatId, "Unknown command. /help for the list.", "");
    }
}

static void telegramTask(void *) {
    // Everything TLS-related lives in this task, so the slow handshake never
    // stalls the web server, DNS proxy or quota loop.
    WiFiClientSecure secured;
    secured.setInsecure();   // fine for a home project; pin Telegram's root cert for anything more
    String token = settings_getTelegramToken();
    String owner = settings_getTelegramChatId();
    UniversalTelegramBot bot(token, secured);
    Serial.println("[TG] bot task running");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TELEGRAM_POLL_MS));
        if (!wifiRepeater_isUpstreamConnected()) continue;

        int updates = bot.getUpdates(bot.last_message_received + 1);
        while (updates) {
            for (int i = 0; i < updates; i++) {
                if (bot.messages[i].chat_id != owner) continue;   // only the owner may command the device
                handleCommand(bot, bot.messages[i].chat_id, bot.messages[i].text);
            }
            updates = bot.getUpdates(bot.last_message_received + 1);
        }
    }
}

void telegramBot_init() {
    if (!settings_telegramConfigured()) {
        Serial.println("[TG] no bot token/chat id saved - Telegram disabled");
        return;
    }
    xTaskCreate(telegramTask, "telegram", 12288, nullptr, 1, nullptr);
}

#include "telegram_bot.h"
#include "config.h"
#include "settings.h"
#include "quota_manager.h"
#include "diagnostics.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

static WiFiClientSecure secured;
static UniversalTelegramBot *bot = nullptr;
static String ownerChatId;
static uint32_t lastPoll = 0;

static String formatStatus() {
    int count;
    DeviceUsage *devs = quota_getDevices(count);
    String out = "Devices:\n";
    for (int i = 0; i < count; i++) {
        if (!devs[i].active) continue;
        char line[128];
        snprintf(line, sizeof(line), "%s  %.0f/%lu MB  %s%s\n",
                 devs[i].macStr,
                 devs[i].bytesToday / (1024.0 * 1024.0),
                 devs[i].dailyLimitMB,
                 devs[i].blocked ? "[BLOCKED] " : "",
                 devs[i].whitelisted ? "[WHITELIST]" : "");
        out += line;
    }
    return out;
}

static void handleCommand(const String &chatId, String text) {
    text.trim();

    if (text == "/status") {
        bot->sendMessage(chatId, formatStatus(), "");
    } else if (text.startsWith("/block ")) {
        String mac = text.substring(7);
        bot->sendMessage(chatId, quota_block(mac.c_str()) ? "Blocked " + mac : "Not found", "");
    } else if (text.startsWith("/unblock ")) {
        String mac = text.substring(9);
        bot->sendMessage(chatId, quota_unblock(mac.c_str()) ? "Unblocked " + mac : "Not found", "");
    } else if (text.startsWith("/setquota ")) {
        int sp = text.indexOf(' ', 10);
        if (sp < 0) { bot->sendMessage(chatId, "Usage: /setquota AA:BB:CC:DD:EE:FF 500", ""); return; }
        String mac = text.substring(10, sp);
        uint32_t mb = text.substring(sp + 1).toInt();
        bot->sendMessage(chatId, quota_setLimit(mac.c_str(), mb) ? "Quota updated" : "Not found", "");
    } else if (text.startsWith("/ping ")) {
        String host = text.substring(6);
        bot->sendMessage(chatId, diagnostics_ping(host.c_str()), "");
    } else if (text == "/help" || text == "/start") {
        bot->sendMessage(chatId,
            "/status\n/block <mac>\n/unblock <mac>\n/setquota <mac> <mb>\n/ping <host>", "");
    } else {
        bot->sendMessage(chatId, "Unknown command. /help for the list.", "");
    }
}

void telegramBot_init() {
    secured.setInsecure(); // simplest path for a personal/home project;
                            // pin Telegram's root cert instead for production use
    String token = settings_getTelegramToken();
    ownerChatId = settings_getTelegramChatId();
    bot = new UniversalTelegramBot(token, secured);
    Serial.println("[TG] bot ready");
}

void telegramBot_loop() {
    if (millis() - lastPoll < TELEGRAM_POLL_MS) return;
    lastPoll = millis();

    int updateCount = bot->getUpdates(bot->last_message_received + 1);
    while (updateCount) {
        for (int i = 0; i < updateCount; i++) {
            String chatId = bot->messages[i].chat_id;
            if (chatId != ownerChatId) continue; // only respond to the owner
            handleCommand(chatId, bot->messages[i].text);
        }
        updateCount = bot->getUpdates(bot->last_message_received + 1);
    }
}

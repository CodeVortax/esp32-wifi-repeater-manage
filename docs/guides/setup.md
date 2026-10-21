# Setup

## First run

1. Power the board. It opens a WiFi network called **`AOSP`** (password `12345678`).
2. Join it and open **http://192.168.4.1**.
3. Log in: user `admin`, password `admin1234`.
4. Open **Settings** and fill in:
   - **Home WiFi name and password**: the router you want to extend.
   - **Repeater name and password**: the network your devices will join (8-63 characters).
   - **Dashboard password** (8-64 characters). The yellow warning disappears once you change it.
5. Press **Save & reboot**. After about 10 seconds the repeater joins your router.

Empty fields keep their current value. Nothing is saved if any field is invalid, and you are told which one.

## Telegram (optional)

1. Create a bot with `@BotFather` and copy its token (`123456:ABC...`).
2. Send your bot any message, then open `https://api.telegram.org/bot<token>/getUpdates` and read `chat.id`.
3. Put both in **Settings**. The commands are listed in the [README](../../README.md#telegram-commands).

Only that chat ID is obeyed.

## Time zone

The daily reset happens at local midnight. Change `TZ_INFO` in `include/config.h` for another country and rebuild. Examples: `UTC0`, `CET-1CEST,M3.5.0,M10.5.0/3`, `EST5EDT,M3.2.0,M11.1.0`.

## If you are locked out

Hold the **BOOT** button for 5 seconds while the board runs. It wipes the saved settings and restarts with the defaults from `include/config.h`. Device quotas are kept.

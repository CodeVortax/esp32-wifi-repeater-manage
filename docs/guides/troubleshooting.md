# Troubleshooting

Open the serial monitor first: `./build.sh monitor` (115200 baud). The log tells you most of what you need.

| Symptom | Check |
|---|---|
| Devices connect but have no internet | Log should show `[STA] connected` then `[NAT] NAPT enabled`. If not, the home WiFi name or password is wrong: fix it in Settings. |
| Counters stay at 0 | Log should show `[QUOTA] AP traffic hooks installed`. If it says it could not find the AP netif, open an issue with your board and the full boot log. |
| Cannot open the dashboard | Join the repeater WiFi and use `http://192.168.4.1`. Browsers may try `https`; type `http://`. |
| Forgot the dashboard password | Hold **BOOT** for 5 seconds. See [Setup](setup.md#if-you-are-locked-out). |
| The repeater WiFi has the wrong name or password | The saved AP password was invalid and the defaults (`AOSP` / `12345678`) were used. Set a valid one in Settings. |
| Daily reset happens at the wrong hour | Check `TZ_INFO` in `include/config.h`, and that NTP can be reached (the home WiFi needs internet). |
| Domains are missing for some devices | The device uses DNS over HTTPS or a fixed DNS server. It never asks the ESP32. |
| Telegram does nothing | The token and chat ID must both be saved. The log says `Telegram disabled` otherwise. Send `/start` to your bot first. |
| Build fails with `undefined reference to ip_napt_enable` | See "NAPT is missing" in [Building](building.md). |
| Slow internet | Normal for NAT on an ESP32. A few Mbit/s up to about 20 is expected. |

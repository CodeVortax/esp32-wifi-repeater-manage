# ESP32 WiFi Repeater — Quota & Monitoring

## هيكل المشروع
```
esp32-wifi-repeater/
├── platformio.ini        # إعدادات البورد والمكتبات
├── build.sh               # سكريبت البناء/الرفع
├── include/config.h       # كل الإعدادات (WiFi, quota, telegram)
├── src/
│   ├── main.cpp
│   ├── wifi_repeater.*    # AP+STA + تفعيل NAPT
│   ├── quota_manager.*    # عد البايتات لكل جهاز + block/whitelist + NVS
│   ├── dns_monitor.*      # DNS proxy بيسجل الدومينات
│   ├── web_server.*       # REST API + WebSocket
│   ├── telegram_bot.*     # أوامر تليجرام
│   └── diagnostics.*      # ping
└── data/index.html        # واجهة الويب (SPIFFS)
```

## قبل ما تبني
اسم شبكة الريبيتر الافتراضي **AOSP** والباسورد الافتراضي **12345678** (معرّفين في `include/config.h`) — دول بس القيم الأولى؛ بعد الفلاش تقدر تغيّر أي حاجة (شبكة البيت، شبكة الريبيتر، توكن التليجرام) من صفحة **Settings** في واجهة الويب نفسها من غير ما تعيد الفلاش. أول تشغيل، لو الشبكة الأساسية (`STA_SSID`) لسه بالقيمة الافتراضية `YOUR_HOME_WIFI`، هتلاقي الجهاز مش هيتصل بالإنترنت لحد ما تدخل بيانات شبكتك من صفحة الـ Settings ويعمل reboot تلقائي.

لو عايز تغيّر القيم الأولى قبل الفلاش أصلاً (اختياري)، عدّل `include/config.h`:
- `STA_SSID` / `STA_PASSWORD` — الراوتر الأساسي
- `AP_SSID` / `AP_PASSWORD` — شبكة الريبيتر اللي الأجهزة هتتصل بيها
- `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID`
- `DAILY_QUOTA_MB_DEFAULT` — الافتراضي 1024 (1GB)

## البناء والرفع
```bash
chmod +x build.sh
./build.sh              # compile فقط
./build.sh uploadfs     # ارفع data/index.html على الـ SPIFFS (مرة واحدة، وبعد أي تعديل في الواجهة)
./build.sh upload       # ارفع الكود على البورد
./build.sh monitor      # سيريال مونيتور
```

## بناء ملف flash واحد جاهز (من غير USB على Termux)
بعد `pio run` (أو `./build.sh`) لو نجح، هيتعمل تلقائيًا ملف واحد مدموج:
```
.pio/build/esp32dev/merged-firmware.bin
```
ده الملف اللي تحتاجه فقط للفلاش — فيه الـ bootloader + partition table + boot_app0 + firmware كلهم مدموجين، ويتفلش بأمر واحد بادئ من offset `0x0`.

**طرق الفلاش بيه من غير `pio run -t upload`:**
1. **من متصفح Chrome على الموبايل نفسه** (الأسهل): استخدم [ESP Web Tools](https://web.esphome.io/) أو [Espressif's ESP Launchpad](https://espressif.github.io/esp-launchpad/) — بتدعم WebSerial، وصّل الـ ESP32 بكابل OTG للموبايل، اختار الملف `merged-firmware.bin`، وفلاش من عنوان `0x0`.
2. **من جهاز PC**: انسخ الملف على الكمبيوتر وشغّل:
   ```
   esptool.py --chip esp32 --port COMx write_flash 0x0 merged-firmware.bin
   ```
3. **من تطبيق أندرويد مخصص**: تطبيقات زي "ESP32 Flash Tool" أو "Espressif ESP Tool" بتقبل ملف bin واحد وتفلشه عبر USB Serial مباشرة من غير Termux خالص.

## نقاط مهمة لازم تعرفها
1. **الأداء**: NAPT على ESP32 العادي محدود (غالبًا أقل من 5Mbps). لو محتاج throughput أعلى فكر في ESP32-S3.
2. **عد البايتات**: بيتم عن طريق promiscuous mode على نفس القناة اللي الـ AP شغال عليها (بتتزبط تلقائيًا مع قناة الـ STA). دقة كويسة لغرض تطبيق الكوتة، مش عداد فوترة دقيق 100%.
3. **الدومينات عبر DoH**: لو جهاز بيستخدم DNS-over-HTTPS (Chrome/Android بيعملوا كده افتراضيًا أحيانًا) هيتخطى الـ DNS proxy بتاعنا. لازم تعمل block لـ IPs بتوع Google/Cloudflare DoH لو عايز تضمن كل الدومينات تتسجل.
4. **الـ Block الفعلي**: بيتم عن طريق `esp_wifi_deauth_sta` لحظة ما الجهاز يتصل لو هو في الـ blocklist، أو لحظة ما يضرب الكوتة. الجهاز هيتقطع من الشبكة تمامًا مش بس يتبطأ.
5. **NVS**: العدادات والحدود بتتحفظ كل 60 ثانية، لكن جدول الأجهزة النشطة بيتبني من الهواء (أول ما جهاز يظهر يتعمله slot). لو عايز جدول أجهزة معروفة ثابت من أول ما البورد يشتغل، ده تحتاج تضيفه في `quota_loadFromNvs()`.
6. **صفحة الـ Settings**: أي حقل تسيبه فاضي بيفضل زي ما هو من غير تغيير — مش هيتصفر. لما تحفظ تغيير حقيقي، الجهاز بيعمل `ESP.restart()` تلقائي بعد ثانية عشان الإعدادات الجديدة تتطبق (شبكة جديدة، توكن تليجرام جديد، إلخ). الباسوردات والتوكن متخزنين في NVS كنص عادي (مش مشفرين) — طبيعي لمشروع شخصي، بس متعتمدش عليه لو الجهاز هيتعرض لأشخاص تانيين فعليًا.

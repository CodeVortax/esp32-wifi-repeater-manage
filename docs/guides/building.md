# Building

## Requirements

- Python 3 and PlatformIO: `pip install -U platformio`
- Internet on the first build (platform, toolchain and libraries are downloaded and cached)

## Commands

```bash
./build.sh            # compile only
./build.sh merge      # firmware + web UI -> dist/esp32dev-merged.bin
./build.sh bin        # merge, then copy to your phone's Downloads (Termux)
./build.sh ota        # copy ota-firmware.bin and ota-ui.bin to Downloads (Termux)
./build.sh upload     # flash the firmware over USB
./build.sh uploadfs   # upload the web UI (data/) over USB
./build.sh monitor    # serial monitor
./build.sh clean
```

`./build.sh merge` runs `pio run -t webflash`. It builds the firmware and the SPIFFS image, then merges bootloader, partition table, app and web UI into **one file** that you flash at `0x0`. A `.sha256` file is written next to it. Offsets and flash size come from the board and `partitions.csv`, so there is nothing to edit when you change the partition table.

## Pinned versions

| | |
|---|---|
| Platform | pioarduino `55.03.311` |
| Arduino-ESP32 | 3.3.11 |
| ESP-IDF | 5.5.5 |

The platform URL in `platformio.ini` is pinned on purpose: the old `stable` link changes under you. To upgrade, change the version in the URL.

## Termux

```bash
pkg install python git
pip install -U platformio
termux-setup-storage          # once, to get ~/storage/downloads
./build.sh bin
```

The `.bin` lands in `~/storage/downloads/esp32-wifi-repeater.bin`. PlatformIO's toolchain is built for desktop Linux, so on some phones the build only works inside a proot Linux or in CI. If Termux cannot build it, let GitHub Actions do it and download the artifact.

## Continuous integration

`.github/workflows/build-firmware.yml` runs on pushes to `master`, pull requests, tags `v*` and manual dispatch. It installs PlatformIO, runs `pio run -t webflash`, checks that the image fits 4 MB, and uploads `esp32-wifi-repeater-<ref>.bin` and its `.sha256` as the `firmware` artifact. A tag like `v0.2.0` also creates a GitHub Release.

## NAPT is missing

NAPT comes with the precompiled Arduino-ESP32 3.x libraries. If the link step fails with `undefined reference to ip_napt_enable`, build with a hybrid setup instead. In `platformio.ini` set:

```ini
framework = arduino, espidf
custom_sdkconfig =
    CONFIG_LWIP_IP_FORWARD=y
    CONFIG_LWIP_IPV4_NAPT=y
```

Hybrid builds are much slower, so only do this if you need it.

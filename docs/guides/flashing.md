# Flashing

You need the single file `esp32-wifi-repeater.bin` (see [Building](building.md)). It already contains the bootloader, partition table, firmware and web UI, and is written at offset **`0x0`**.

## From Chrome (easiest)

Open [ESP Web Tools](https://web.esphome.io/) or [Espressif's ESP Launchpad](https://espressif.github.io/esp-launchpad/) in Chrome or Edge. They use WebSerial. Connect the board, choose the file and write it at `0x0`. This also works from an Android phone with a USB-OTG cable.

## From a PC

```bash
pip install esptool
esptool.py --chip esp32 --port COM3 write_flash 0x0 esp32-wifi-repeater.bin      # Windows
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x0 esp32-wifi-repeater.bin   # Linux
```

If the upload does not start, hold **BOOT** while it says "Connecting...".

## Check the download

```bash
sha256sum -c esp32-wifi-repeater-<ref>.bin.sha256
```

## Development flashing

With PlatformIO: `./build.sh upload` for the firmware and `./build.sh uploadfs` for the web UI.

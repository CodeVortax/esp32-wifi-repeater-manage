"""
Adds the `webflash` target:

    pio run -t webflash

It builds the firmware and the SPIFFS image (the web UI in data/), then merges
bootloader + partition table + app + SPIFFS into ONE file:

    dist/<env>-merged.bin

Flash that single file at offset 0x0 (ESP Web Tools, esptool, ...).
Every offset and size comes from the board and from the partition table, so
changing min_spiffs.csv or the board needs no edit here.
"""
import hashlib
import subprocess
import sys
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

PROJECT_DIR = Path(env["PROJECT_DIR"])              # noqa: F821
BUILD_DIR = Path(env.subst("$BUILD_DIR"))           # noqa: F821
PIOENV = env["PIOENV"]                              # noqa: F821
DIST_DIR = PROJECT_DIR / "dist"


def parse_int(text: str) -> int:
    text = text.strip().upper()
    if text.startswith("0X"):
        return int(text, 16)
    mult = 1
    if text.endswith("K"):
        mult, text = 1024, text[:-1]
    elif text.endswith("M"):
        mult, text = 1024 * 1024, text[:-1]
    return int(text) * mult


def read_partitions(csv_path: Path) -> dict:
    """name -> (offset, size) from an ESP-IDF partition CSV."""
    parts, cursor = {}, 0x9000          # first partition starts after the partition table
    for raw in csv_path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        cols = [c.strip() for c in line.split(",")]
        if len(cols) < 5:
            continue
        name, offset, size = cols[0], cols[3], parse_int(cols[4])
        cursor = parse_int(offset) if offset else (cursor + 0xFFF) & ~0xFFF
        parts[name] = (cursor, size)
        cursor += size
    return parts


def find(name: str) -> Path:
    direct = BUILD_DIR / name
    if direct.exists():
        return direct
    for hit in BUILD_DIR.rglob(name):
        return hit
    raise SystemExit(f"[webflash] {name} not found in {BUILD_DIR} - did the build fail?")


def ensure_spiffs() -> None:
    """Build the web-UI image (data/ -> spiffs.bin) unless it is up to date.

    Done here instead of through a SCons target dependency: that dependency did
    not run reliably on pioarduino, which left spiffs.bin missing."""
    data_dir = PROJECT_DIR / "data"
    image = BUILD_DIR / "spiffs.bin"
    if not data_dir.is_dir():
        raise SystemExit("[webflash] no data/ folder, nothing to build the web UI from")
    newest = max((f.stat().st_mtime for f in data_dir.rglob("*") if f.is_file()), default=0)
    if image.exists() and image.stat().st_mtime >= newest:
        print("[webflash] spiffs.bin is up to date")
        return
    print("[webflash] building the web UI image (buildfs)...")
    python = env.subst("$PYTHONEXE")                            # noqa: F821
    subprocess.check_call([python, "-m", "platformio", "run", "-d", str(PROJECT_DIR),
                           "-e", PIOENV, "-t", "buildfs"])


def find_boot_app0():
    """boot_app0.bin makes the bootloader start app0 (it lives in otadata)."""
    for root in (BUILD_DIR,):
        for hit in root.rglob("boot_app0.bin"):
            return hit
    try:
        pkg = env.PioPlatform().get_package_dir("framework-arduinoespressif32")   # noqa: F821
        cand = Path(pkg) / "tools" / "partitions" / "boot_app0.bin"
        if cand.exists():
            return cand
    except Exception:
        pass
    return None


def merge(*_args, **_kwargs):
    ensure_spiffs()
    board = env.BoardConfig()                                   # noqa: F821
    chip = board.get("build.mcu", "esp32").lower()
    flash_size = board.get("upload.flash_size", "4MB")
    flash_mode = board.get("build.flash_mode", "dio")
    f_flash = str(board.get("build.f_flash", "40000000L")).rstrip("L")
    flash_freq = "80m" if f_flash.isdigit() and int(f_flash) >= 80_000_000 else "40m"

    csv_path = PROJECT_DIR / env.GetProjectOption("board_build.partitions")   # noqa: F821
    parts = read_partitions(csv_path)
    for needed in ("app0", "spiffs"):
        if needed not in parts:
            raise SystemExit(f"[webflash] partition '{needed}' missing in {csv_path.name}")

    # The 2nd-stage bootloader lives at 0x1000 on ESP32/ESP32-S2, 0x0 on newer chips.
    bootloader_offset = 0x1000 if chip in ("esp32", "esp32s2") else 0x2000 if chip == "esp32c5" else 0x0
    pieces = [
        (bootloader_offset, find("bootloader.bin")),
        (0x8000, find("partitions.bin")),
        (parts["app0"][0], find("firmware.bin")),
        (parts["spiffs"][0], find("spiffs.bin")),
    ]

    boot_app0 = find_boot_app0()
    if boot_app0 and "otadata" in parts:
        pieces.insert(2, (parts["otadata"][0], boot_app0))
    else:
        print("[webflash] note: boot_app0.bin not found, otadata left blank (the bootloader still starts app0)")

    spiffs_size = [p for o, p in pieces if o == parts["spiffs"][0]][0].stat().st_size
    if spiffs_size > parts["spiffs"][1]:
        raise SystemExit(f"[webflash] web UI image ({spiffs_size} B) is bigger than the spiffs partition")

    DIST_DIR.mkdir(parents=True, exist_ok=True)
    out = DIST_DIR / f"{PIOENV}-merged.bin"

    python = env.subst("$PYTHONEXE")                            # noqa: F821
    uploader = env.subst("$UPLOADER")                           # noqa: F821
    tool = [python, uploader] if uploader and Path(uploader).exists() else [python, "-m", "esptool"]

    cmd = tool + [
        "--chip", chip, "merge_bin",
        "--fill-flash-size", flash_size,
        "-o", str(out),
        "--flash_mode", flash_mode,
        "--flash_freq", flash_freq,
        "--flash_size", flash_size,
    ]
    for offset, path in pieces:
        cmd += [f"0x{offset:X}", str(path)]

    print("[webflash]", " ".join(cmd))
    subprocess.check_call(cmd)

    digest = hashlib.sha256(out.read_bytes()).hexdigest()
    (DIST_DIR / f"{out.name}.sha256").write_text(f"{digest}  {out.name}\n")
    print(f"[webflash] OK -> {out}  ({out.stat().st_size} bytes)")
    print(f"[webflash] flash it at offset 0x0:  esptool.py write_flash 0x0 {out.name}")
    sys.stdout.flush()


env.AddCustomTarget(                                            # noqa: F821
    name="webflash",
    dependencies=["buildprog"],
    actions=[merge],
    title="WebFlash",
    description="Build firmware + web UI and merge them into dist/<env>-merged.bin",
)

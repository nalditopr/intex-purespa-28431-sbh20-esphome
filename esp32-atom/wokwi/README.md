# Offline simulation & debugging (Wokwi / QEMU)

Boot and debug the firmware **without the spa or a USB cable** by running the real firmware
image in an ESP32 emulator and reading its serial output — including any `Guru Meditation`
panic + backtrace. Useful for reproducing a crash, sanity-checking a build, or watching the
boot sequence.

> The spa serial bus is **not** simulated, so the clock/data auto-detect will sit at
> `no bus activity yet` — that's expected. This reproduces anything that happens regardless
> of live panel data (boot, WiFi, the climate poll, …), not the live-bus capture itself.
>
> This harness is what pinned the one real ESP32 crash this port hit: the climate's
> `Parented<>` parent was never wired in codegen, so `SBHClimate::update()` dereferenced a
> null instance pointer (`LoadProhibited`, `EXCVADDR 0x14`). Fixed by `clim.set_parent(var)`
> in `__init__.py`.

## Option A — Wokwi (no cable)

> Run the commands below from **`esp32-atom/`** (the esphome config dir), so the relative
> `.esphome/…` and `wokwi` paths resolve.

1. Free account at https://wokwi.com → get a CLI token. Install the CLI:
   `curl -L https://wokwi.com/ci/install.sh | sh`  (or the VS Code "Wokwi Simulator" extension)
2. Build the firmware on the box that has esphome: `esphome compile spa-atom.yaml`
3. Confirm the artifacts and fix the paths in `wokwi.toml` if your `name:` substitution differs:
   `ls .esphome/build/intex-spa-atom/.pioenvs/intex-spa-atom/firmware*.bin`
   - Wokwi wants the merged flash image (`firmware.factory.bin`). If it's missing, merge one:
     `esptool.py --chip esp32 merge_bin -o firmware.factory.bin @flash_args` (from `.pioenvs/<name>/`)
4. To let WiFi associate in the sim, set `wifi: { ssid: "Wokwi-GUEST", password: "" }` (optional —
   not needed just to watch an early boot).
5. Run headless and capture serial:
   `WOKWI_CLI_TOKEN=<token> wokwi-cli --timeout 60000 --serial-log-file serial.txt wokwi`
6. Read `serial.txt`. Decode any backtrace addresses against the ELF:
   `xtensa-esp-elf-addr2line -e .esphome/build/intex-spa-atom/.pioenvs/intex-spa-atom/firmware.elf 0x<addr> …`
   (or paste into the ESP Exception Decoder).

## Option B — Espressif QEMU + GDB (full backtrace, breakpoints)

1. Get Espressif's QEMU fork (https://github.com/espressif/qemu releases, `qemu-system-xtensa`).
2. Make a 4 MB flash image from the esphome build (bootloader @ 0x1000, partitions @ 0x8000,
   app @ 0x10000), or pad `firmware.factory.bin` to 4 MB.
3. `qemu-system-xtensa -nographic -machine esp32 -m 4M -drive file=flash.bin,if=mtd,format=raw -s -S`
4. `xtensa-esp-elf-gdb firmware.elf -ex "target remote :1234" -ex c` → on a panic, `bt` gives the frame.

## Reproducing the live-bus path

The emulator can't drive the 140 kHz panel clock, so the capture ISR never attaches. To
exercise the ISR↔loop interaction off-spa, a throwaway diagnostic build can drive the clock
pin internally (so `clockRisingISR` fires as a real interrupt) and fake a powered panel —
that is how the climate-parent crash above was reproduced on a bench Atom over USB before the
fix landed.

## Files

- `wokwi.toml` — points Wokwi at the firmware (`firmware.factory.bin`) + ELF.
- `diagram.json` — a bare ESP32 dev board with the serial monitor wired to `TX`/`RX`.

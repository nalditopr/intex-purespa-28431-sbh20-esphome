# Offline debugging: catch the boot crash without hardware

The boot crash is deterministic and not in the control logic (a native UBSan/`-ftrapv`
fuzz of `tickControls()`/`setTargetTemperature()` is clean across millions of runs), so to
find it we boot the *real firmware image* in an emulator and read the serial panic +
backtrace — the same thing a USB cable would give, without the cable.

The spa serial bus is **not** simulated, so this only reproduces crashes that happen
regardless of live panel data — which is exactly this one (it crashes at idle boot).

## Option A — Wokwi (recommended, no cable)

1. Free account at https://wokwi.com → get a CLI token. Install the CLI:
   `curl -L https://wokwi.com/ci/install.sh | sh`  (or the VS Code "Wokwi Simulator" extension)
2. Build the **crashing** firmware (non-blocking build checked out) on the box that has esphome:
   `esphome compile spa-atom.yaml`
3. Confirm the build artifacts exist and fix the paths in `wokwi.toml` if your `name:` differs:
   `ls .esphome/build/intex-spa-atom/.pioenvs/intex-spa-atom/firmware*.bin`
   - If there is no `firmware.factory.bin`, merge one:
     `esptool.py --chip esp32 merge_bin -o firmware.factory.bin @flash_args`  (run from `.pioenvs/<name>/`)
4. (Only if the crash needs WiFi up) temporarily set `wifi: { ssid: "Wokwi-GUEST", password: "" }`.
   For an early boot crash you usually don't need this.
5. Run headless and capture serial:
   `WOKWI_CLI_TOKEN=<token> wokwi-cli --timeout 60000 --serial-log-file serial.txt esp32-atom/wokwi`
6. Read `serial.txt` for the `Guru Meditation Error` / backtrace. Decode addresses:
   `xtensa-esp32-elf-addr2line -e .esphome/build/intex-spa-atom/.pioenvs/intex-spa-atom/firmware.elf 0x<addr> ...`
   (or paste into the ESP Exception Decoder). Send me the decoded backtrace.

## Option B — Espressif QEMU + GDB (full backtrace, breakpoints)

1. Get Espressif's QEMU fork (https://github.com/espressif/qemu releases, `qemu-system-xtensa`).
2. Make a 4 MB flash image from the esphome build (bootloader@0x1000, partitions@0x8000, app@0x10000),
   or use `firmware.factory.bin` padded to 4 MB.
3. `qemu-system-xtensa -nographic -machine esp32 -m 4M -drive file=flash.bin,if=mtd,format=raw -s -S`
4. `xtensa-esp32-elf-gdb firmware.elf -ex "target remote :1234" -ex c`  → on the panic, `bt` gives the exact frame.

Either way the deliverable is the backtrace; that pins the faulting line so the non-blocking
build can be fixed and re-enabled.

# ESP32 (M5Stack Atom Lite) port

An ESP32 port of the `intexsbh20` ESPHome component, targeting the **M5Stack Atom
Lite** (ESP32-PICO-D4). The upstream components are ESP8266 (`piitaya`) or
RP2040/Pico W (`RealByron`) only — this is an ESP32-native firmware for the SB-H20
panel.

> ✅ **Status: working — validated on a real Intex 28431E PureSpa Plus (SB-H20).**
> Water temperature reads correctly and every control works from Home Assistant —
> Power, Bubble and Filter, temperature (setpoint read **and** change), and Heat mode — plus an
> **Error** diagnostic sensor and a **Problem** binary sensor for HA alerts. Every control
> action is **non-blocking**, so nothing stalls the ESPHome loop or drops the API.

![SB-H20 to BSS138 to M5Stack Atom Lite wiring diagram](wiring/atom-wiring.png)

> Open `wiring/atom-wiring.html` for the same diagram interactively.

## Why ESP32 here

The SB-H20 is a continuous, latch-framed serial bus (~16-bit frames). Reading it reliably
*while* running WiFi is the whole challenge. On the **ESP8266** a single core runs WiFi and
the capture, so WiFi preempts the timing and button presses get missed — the well-known
reliability issue.

The **ESP32 has two cores.** This port pins all bus capture to **core 1 (APP_CPU)** while
ESPHome's WiFi runs on **core 0 (PRO_CPU)**, so WiFi can't disturb the receive/transmit
timing — the same isolation the Pico W gets from PIO, on a cheaper, tinier, 5 V-friendly
board.

## How it works (capture architecture)

1. **Auto-detects which pin is the clock.** At boot a core-1 task briefly measures both
   signal pins; the clock has far more edges than data, so the **clock/data wiring order
   doesn't matter** — only LATCH is fixed.
2. **Interrupt-driven capture.** It then installs GPIO interrupts (clock-rising +
   latch-falling) on core 1, exactly like the proven D1-mini build. A hardware interrupt
   latches every clock edge regardless of CPU load, and releases the DATA line precisely
   after a button reply — so transmitting a press never corrupts the frames being received.
3. **Light decode in the ISR, heavy decode deferred.** LED/button frames decode in the
   ISR; the 7-segment display decode runs in the main loop. Frames that don't form a
   plausible value (a temperature, a blank, an error, or a valid LED word) are rejected,
   which shrugs off the small amount of residual line noise.

## Control model (non-blocking)

Every control action **queues work and returns immediately** — the ESPHome loop is never
blocked waiting on the panel, so the Home Assistant / API connection stays responsive even
mid-press.

- **Toggles** (Power / Filter / Bubble — Heat is driven by the climate Off/Heat mode, not a
  separate switch) queue a single button press; the panel's new LED state is read back
  asynchronously and published when it changes.
- **Temperature** uses a **closed-loop sequencer**: read the panel's current setpoint, press
  once toward the target, invalidate the cached reading, wait for the panel to re-show the
  new value, then re-read and repeat until reached. Reading back after every press makes it
  self-correcting — a press that only *reveals* the setpoint (without changing it) is simply
  retried, and it can't overshoot — and the work is spread across loop iterations.
- The **automatic setpoint read** (used to learn the target after boot, since the panel only
  flashes it briefly) is likewise a single queued press, not a busy-wait.

This matters because a blocking control stalls the loop for ~1–2 s waiting on the panel's
beep ack, which can drop the native API and briefly flip the device to "unavailable".

## Entities

The component exposes these Home Assistant entities — list only the ones you want under the
`intexsbh20:` block in `spa-atom.yaml` (each is optional). Enabling the `climate`, `switch`,
`sensor` or `text_sensor` entities also requires the matching bare top-level platform key
(`climate:` / `switch:` / `sensor:` / `text_sensor:` — see the top of `spa-atom.yaml`); only
`binary_sensor` is auto-loaded for you.

| Entity | Type | Notes |
|--------|------|-------|
| **Thermostat** | `climate` | Off / Heat, current + target temperature, heating action (idle/heating) |
| **Power**, **Filter**, **Bubble** | `switch` | the three panel toggles |
| **Water temperature** | `sensor` | °C, decoded from the panel's 7-segment display |
| **Error** | `text_sensor` | the **English fault message** for the displayed code: `no water flow` (E90), `water temp too low` (E94), `water temp too high` (E95), `system error` (E96), `dry fire protection` (E97), `water temp sensor error` (E99), `heating aborted after 72h` (END); blank when clear, `error` for an unrecognised code. The build is fixed to English — automate on the message text, not the `Exx` code |
| **Problem** | `binary_sensor` | `device_class: problem` — **on** when the panel shows an `Exx` fault, **auto-clears** when a normal temperature returns. Auto-loaded, so no bare `binary_sensor:` is needed in your YAML |

## Wiring (Atom Lite)

All three signals must be on **GPIO < 32** (the fast path uses the 32-bit GPIO registers).

- **LATCH → G23** (the per-frame strobe: idle-high with brief low pulses — see the
  multimeter ID table in the [main README](../README.md)).
- **CLOCK and DATA → G19 and G22, in *either* order** — the firmware auto-detects them.

| Spa wire | Function | BSS138 | Atom Lite pin |
|----------|----------|--------|---------------|
| red\*    | +5 V  | HV  | **5V** |
| green\*  | GND   | GND | **GND** |
| signal   | CLK / DATA | HV1 ↔ LV1 | **G19** ┐ either order |
| signal   | DATA / CLK | HV2 ↔ LV2 | **G22** ┘ (auto-detected) |
| signal   | LATCH | HV3 ↔ LV3 | **G23** |

\* *Wire colors vary by production batch — identify by function with a meter, don't trust
the color. On the unit pictured, GND was green and +5 V was red; the three signal wires
were white/blue, yellow and black.*

- BSS138 **LV ← Atom 3V3**, **HV ← spa 5 V**. All grounds common.
- Power the Atom from spa **5 V** (its 5V pin); its onboard regulator makes 3V3.
- The ESP8266 build's **470 Ω series resistors are optional** here — G19/G22/G23 aren't
  ESP32 strapping pins, so the boot back-power issue doesn't occur. Harmless to keep.

`spa-atom.yaml` sets `clock_pin: 19`, `data_pin: 22`, `latch_pin: 23`. Because clock/data
auto-detect, you can swap the first two freely; just keep latch on `latch_pin`.

## Power

Measured on the bench (Atom Lite, WiFi connected, `output_power: 12dB` +
`power_save_mode: light`):

| State | Current @ 5 V |
|-------|---------------|
| Steady | **~66 mA** |
| WiFi TX peaks | **~110–120 mA** |

Light enough to run off the spa's 5 V tap with margin. **Fuse the 5 V line at ~250 mA
slow-blow** (≈2× the peak). A **10 µF** cap across the Atom 5V/GND smooths the TX spikes.

## Build / flash

1. Copy `secrets.yaml.example` → `secrets.yaml` next to `spa-atom.yaml` and fill in the four
   keys — `wifi_ssid`, `wifi_password`, `ota_password`, and `api_key` (a base64 32-byte key:
   generate it with the ESPHome dashboard key icon, or
   `python -c "import secrets,base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"`).
2. `esphome run spa-atom.yaml` (first compile pulls the ESP32 toolchain).
3. Flash over USB-C, confirm WiFi + entities, then wire to the spa (5 V — **don't** power
   USB and the spa at the same time). Updates after that go over OTA.

**Build on your Home Assistant / ESPHome box**, where the platform installs cleanly. Two
Windows gotchas if you build there:
- An **accented character in the user path** (e.g. `C:\Users\José\…`) breaks the xtensa
  linker (it truncates the path). Build from an ASCII path / set `PLATFORMIO_CORE_DIR` to one.
- A flaky package mirror on first install can leave `framework-arduinoespressif32-libs` an
  empty stub (missing WiFi/PHY blobs → `cannot find -lcore/-lphy/...`). Re-run the platform
  install if you see that.

## What changed vs. the ESP8266 component

| Area | ESP8266 original | ESP32 port |
|------|------------------|------------|
| Pins | hard-coded GPIO 14/12/13 | YAML `clock_pin`/`data_pin`/`latch_pin`; **clock/data auto-detected** |
| GPIO access | `digitalRead()` | direct `GPIO_IN_REG` / `GPIO_ENABLE_W1TS/W1TC` |
| Capture core | the only core | GPIO ISRs installed from a task **pinned to core 1** |
| CPU clock | must force 160 MHz | n/a (240 MHz, core-isolated) |
| Control actions | blocking (busy-wait for the panel ack) | **all non-blocking** — toggles queue one press; temperature uses a closed-loop read→press→re-read sequencer |
| Entities | climate + switches + temp | adds an **Error** text sensor and a **Problem** binary sensor (`device_class: problem`, auto-clears) |
| Framework | — | **`arduino`** required |

Receive decoding, frame parsing, button logic and the climate/switch/sensor entities are
otherwise the proven `piitaya/esphome-intexsbh20` logic.

## Build validation status

| Check | Result |
|-------|--------|
| Host unit tests (`test/test_decode.cpp`, decode math) | ✅ pass (g++) |
| `esphome config` (schema / pins / codegen) | ✅ valid |
| Compile on the ESP32 toolchain | ✅ zero errors |
| Flashable `firmware.bin` | ✅ builds + flashes (HA/ESPHome box) |
| **Runs on a real spa** | ✅ temp + all controls (non-blocking) + Error/Problem sensors on an Intex 28431E PureSpa Plus |

## Notes / limitations

- Diagnostics log at **VERBOSE** level — set the logger to `VERBOSE` to see frame/decode
  stats; normal logs stay quiet.
- All control actions are **non-blocking** (see *Control model* above) — toggles, temperature
  changes and the automatic setpoint read all queue work and return immediately, so tapping a
  control never stalls the loop or drops the API.
- `framework: arduino` is required (`attachInterruptArg`, `pinMode`, `REG_READ/WRITE`); an
  esp-idf variant would need rework.
- Pins are constrained to GPIO 0–31. The Atom exposes G19/21/22/23/25 (+ Grove G26/G32) —
  all fine.
- **Offline boot/crash debugging without the spa** — run the firmware in a simulator and read
  the serial backtrace: see [`wokwi/README.md`](wokwi/README.md).

## License

The `SBH20IO.*` receive code is derived from DIYSCIP and is licensed
**CC-BY-NC-SA-4.0** (non-commercial). The rest follows the upstream
`piitaya/esphome-intexsbh20`. See file headers.

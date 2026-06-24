# ESP32 (M5Stack Atom Lite) port — EXPERIMENTAL

An ESP32 port of the `intexsbh20` ESPHome component, targeting the **M5Stack Atom
Lite** (ESP32-PICO-D4). This is the first ESP32-native firmware for the SB-H20
panel — the upstream components are ESP8266 (`piitaya`) or RP2040/Pico W
(`RealByron`) only.

> ⚠️ **Status: experimental — not yet hardware-validated.** The code compiles
> against the logic of the proven ESP8266 component but has **not** been run on a
> real spa yet. Test on the bench/at the spa before trusting it. Findings and
> fixes welcome.

## Why ESP32 makes sense here

The SB-H20 button protocol is timing-critical (pull DATA low for ~2 µs after the
latch edge). On the **ESP8266** the single core runs WiFi *and* the ISR, so WiFi
preempts the timing and presses get missed — the well-known reliability issue.

The **ESP32 has two cores.** This port installs the clock/latch ISRs from a task
**pinned to core 1 (APP_CPU)**, while ESPHome's WiFi runs on **core 0 (PRO_CPU)**.
WiFi can no longer preempt the receive/transmit timing — the same isolation the
Pico W gets from PIO, but on a cheaper, tinier, 5 V-friendly board.

## What changed vs. the ESP8266 component

| Area | ESP8266 original | ESP32 port |
|------|------------------|------------|
| Pins | hard-coded GPIO 14/12/13 | configurable via YAML (`clock_pin`/`data_pin`/`latch_pin`) |
| ISR reads | `digitalRead()` | direct `GPIO_IN_REG` (fast, IRAM) |
| Button TX | `pinMode(DATA, OUTPUT/INPUT)` | `GPIO_ENABLE_W1TS/W1TC` (fast, IRAM) |
| ISR core | (only core) | installed from a task **pinned to core 1** |
| CPU clock | must force 160 MHz | n/a (240 MHz, core-isolated) |
| Framework | — | **`arduino`** required (uses Arduino HAL) |

Receive decoding, frame parsing, button logic, climate/switch/sensor entities are
**unchanged** from `piitaya/esphome-intexsbh20`.

## Wiring (Atom Lite)

All three signals must be **GPIO < 32** (the fast ISR path uses the 32-bit GPIO
registers). Defaults below.

| Spa wire | Function | BSS138 | Atom Lite pin |
|----------|----------|--------|---------------|
| red    | +5 V  | HV  | **5V** |
| green  | GND   | GND | **GND** |
| white  | CLK   | HV1 ↔ LV1 | **G22** |
| yellow | DATA  | HV2 ↔ LV2 | **G19** |
| black  | LATCH | HV3 ↔ LV3 | **G23** |

- BSS138 **LV ← Atom 3V3**, **HV ← spa 5 V**. All grounds common.
- Power the Atom from spa **5 V** (its 5V pin); its regulator makes 3V3.
- The ESP8266 build's **470 Ω series resistors are optional here** — G19/G22/G23
  are not ESP32 strapping pins, so the boot back-power issue shouldn't occur.
  Harmless to keep as insurance.

## Build / flash

1. Copy your `secrets.yaml` (wifi + api_key + ota_password) next to `spa-atom.yaml`.
2. `esphome run spa-atom.yaml` (first compile pulls the ESP32 toolchain).
3. Flash over USB-C, confirm WiFi + entities, then wire to the spa (5 V, USB out).

## Known caveats / TODO (validate on hardware)

- **Untested on a real spa.** Confirm `water_temperature` decodes and buttons act.
- `framework: arduino` is required (the component uses `attachInterruptArg`,
  `pinMode`, `REG_READ/REG_WRITE`). An esp-idf variant would need rework.
- Button methods block the main loop (inherited from upstream). On ESP32 watch for
  "component took a long time" warnings; if they appear, move the press loop to the
  pinned task.
- Pins are constrained to GPIO 0–31. The Atom exposes G19/21/22/23/25 (+ Grove
  G26/G32) — all fine.
- Verify the DATA idle/pull-up behaviour: DATA must idle HIGH via the bus pull-up
  when the ESP releases it.

## License

The `SBH20IO.*` receive code is derived from DIYSCIP and is licensed
**CC-BY-NC-SA-4.0** (non-commercial). The rest follows the upstream
`piitaya/esphome-intexsbh20` (Apache-2.0). See file headers.

# Intex PureSpa SB-H20 → ESPHome (ESP8266 / Wemos D1 mini)

WiFi + Home Assistant control for the **Intex PureSpa** with the **SB-H20** control
panel (the older, non-WiFi pump) using a **Wemos D1 mini (ESP8266)**, a **BSS138
level shifter**, and the

> **Tested on:** Intex **28431E PureSpa Plus** (SB-H20 panel). The SB-H20 plug/pinout
> is shared with the SSP and SJB models, so this should also apply to SimpleSpa SB-B20,
> SSP-H-20-1, and SJB-HS.

Built with
[`piitaya/esphome-intexsbh20`](https://github.com/piitaya/esphome-intexsbh20)
external component.

This repo is a build log + known-good wiring for that combination, including the
gotchas that took a while to find. It taps the ribbon between the control panel
and the mainboard — no replacement board, no soldering on the spa itself.

> Open `wiring/sbh20-wiring.html` in a browser for the interactive as-built diagram.

---

## Why ESP8266 and not ESP32 / Pico

The SB-H20 button protocol is timing-critical (the panel pulls DATA low for ~2&nbsp;µs
windows, repeated until the mainboard acks). The ESPHome ecosystem for this panel is
**ESP8266-only and must run at 160&nbsp;MHz** to hit that timing. ESP32 has no drop-in
port (no PIO/equivalent wired up), and the Raspberry Pi Pico W version
([`RealByron/PicoW-Intex-PureSpa`](https://github.com/RealByron/PicoW-Intex-PureSpa))
offloads timing to PIO. For an SB-H20, a **$4 D1 mini is the path of least resistance.**

---

## Bill of materials

| Part | Notes |
|------|-------|
| Wemos / LOLIN **D1 mini** (ESP8266) | ESP12/HUZZAH also work |
| **BSS138** 4-channel level shifter | *not* TXS0108E — auto-direction chips fight the open-drain DATA line |
| 3 × **470 Ω** resistors | series on each signal line (see gotcha #4) |
| Cable tap / 3D-printed connector | to splice the panel↔mainboard ribbon |
| Hookup wire, perfboard, IP-rated box | permanent outdoor install |

Total ≈ $8–15.

---

## Wiring (as-built)

**Spa cable colors vary by production batch — verify yours with a meter.** On this
unit the colors were:

| Spa wire | Function | BSS138 | Series R | D1 mini |
|----------|----------|--------|----------|---------|
| red    | +5 V  | HV  | —     | 5V pin |
| green  | GND   | GND | —     | G |
| white  | CLK   | HV1 ↔ LV1 | **470 Ω** | D5 / GPIO14 |
| yellow | DATA  | HV2 ↔ LV2 | **470 Ω** | D6 / GPIO12 |
| black  | LATCH | HV3 ↔ LV3 | **470 Ω** | D7 / GPIO13 |

- BSS138 **HV ← red 5 V**, **LV ← D1 mini 3V3** (both references required).
- All grounds common (spa green ↔ BSS138 GND ↔ D1 mini G).
- The D1 mini is powered **from the spa's 5 V** (red) in normal use.

### Identifying the wires with a multimeter (DC, spa on, ref = GND)

| Reading | Meaning |
|---------|---------|
| steady 5 V | the **supply** (only one wire) |
| 0 V steady + continuity to chassis | **GND** |
| ~2.5 V (50 % duty avg) | **CLK** (free-running clock) |
| ~3 V (mostly high, active) | **DATA** (open-drain, idle high) |
| ~4.5–5 V (idle high, brief pulses) | **LATCH** (per-frame strobe) |

Multiple wires can read ~5 V — those are idle-high signals, not power. The real
supply is the one that's *dead steady*.

---

## Gotchas (the stuff that actually bit us)

1. **Wire colors are not standard.** red≠5V on every unit. Measure. The supply is
   whichever wire is a rock-steady ~5 V; idle-high signals also read ~5 V.
2. **CPU must be 160 MHz.** Reads work at 80 MHz but button TX silently fails.
   Set `board_build.f_cpu: 160000000L` and **Clean Build** (the flag often doesn't
   apply on an incremental build). Verify at runtime with `ESP.getCpuFreqMHz()`.
3. **Use BSS138, not TXS0108E.** The DATA line is open-drain with a pull-up; the
   TXS0108E's auto-direction sensing fights it. BSS138 is purpose-built for this.
4. **470 Ω series resistors on the signal lines.** Without them the ESP8266 may
   **fail to boot with the cables connected** — the 5 V signals back-power the chip
   through the level shifter / GPIO clamp diodes before the rail is up. The resistors
   limit that injection. (Also keep signals on D5/D6/D7 — never the D3/D4/D8 strap pins.)
5. **Never power USB + spa at the same time.** Flash on USB, then run on spa 5 V only.
   Future updates go over **OTA**.

---

## Flashing

1. Copy `esphome/secrets.yaml.example` → `secrets.yaml` and fill in WiFi + a generated
   `api_key` / `ota_password`.
2. First flash over USB (D1 mini only, **not** wired to the spa). Then OTA forever after.
3. Wire to the spa per the table, power from spa 5 V, confirm the `water_temperature`
   entity reads correctly and a button (Power/Bubble) activates.

`esphome/intex-spa.diagnostic.yaml` is a throwaway firmware that turns the three GPIOs
into frequency counters — handy for identifying CLK (highest edge rate) when sorting
the signal wires.

`tools/esp_log_monitor.py` streams the device's logs over the native API from a PC
(no ESPHome CLI needed) — set the address + noise PSK at the top.

---

## Credits

- [`piitaya/esphome-intexsbh20`](https://github.com/piitaya/esphome-intexsbh20) — the ESPHome component
- [`jnsbyr/esp8266-intexsbh20`](https://github.com/jnsbyr/esp8266-intexsbh20) — protocol reverse engineering
- [`RealByron/PicoW-Intex-PureSpa`](https://github.com/RealByron/PicoW-Intex-PureSpa) — Pico W variant

## License

MIT — see [LICENSE](LICENSE).

/*
 * project:  Intex PureSpa SB-H20 WiFi Controller — ESP32 port
 *
 * file:     SBH20IO.cpp
 *
 * encoding: UTF-8
 * created:  14th March 2021
 *
 * Copyright (C) 2021 Jens B.
 *
 * Receive data handling based on code from:
 * DIYSCIP <https://github.com/yorffoeg/diyscip> (c) by Geoffroy HUBERT - yorffoeg@gmail.com
 * DIYSCIP is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0
 * International License. <https://creativecommons.org/licenses/by-nc-sa/4.0/>
 *
 * SPDX-License-Identifier: CC-BY-NC-SA-4.0
 *
 * ESP32 port (2026): pins configurable, ISR uses direct GPIO registers, ISRs
 * installed on core 1 so WiFi (core 0) cannot preempt the timing.
 */

#include "SBH20IO.h"

#include "soc/gpio_reg.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_slave.h"
#include "soc/spi_periph.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "esphome/core/log.h"

// bit mask for LEDs
namespace FRAME_LED
{
  const uint16_t POWER = 0x0001;
  const uint16_t HEATER_ON = 0x0080; // max. 72 h, will start filter, will not stop filter
  const uint16_t NO_BEEP = 0x0100;
  const uint16_t HEATER_STANDBY = 0x0200;
  const uint16_t BUBBLE = 0x0400; // max. 30 min
  const uint16_t FILTER = 0x1000; // max. 24 h
}

namespace FRAME_DIGIT
{
  // bit mask of 7-segment display selector
  const uint16_t POS_1 = 0x0040;
  const uint16_t POS_2 = 0x0020;
  const uint16_t POS_3 = 0x0800;
  const uint16_t POS_4 = 0x0004;
  const uint16_t POS_ALL = POS_1 | POS_2 | POS_3 | POS_4;

  // bit mask of 7-segment display element
  const uint16_t SEGMENT_A = 0x2000;
  const uint16_t SEGMENT_B = 0x1000;
  const uint16_t SEGMENT_C = 0x0200;
  const uint16_t SEGMENT_D = 0x0400;
  const uint16_t SEGMENT_E = 0x0080;
  const uint16_t SEGMENT_F = 0x0008;
  const uint16_t SEGMENT_G = 0x0010;
  const uint16_t SEGMENT_DP = 0x8000;
  const uint16_t SEGMENTS = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F | SEGMENT_G;

  // bit mask of human readable value on 7-segment display
  const uint16_t OFF = 0x0000;
  const uint16_t NUM_0 = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F;
  const uint16_t NUM_1 = SEGMENT_B | SEGMENT_C;
  const uint16_t NUM_2 = SEGMENT_A | SEGMENT_B | SEGMENT_G | SEGMENT_E | SEGMENT_D;
  const uint16_t NUM_3 = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_G;
  const uint16_t NUM_4 = SEGMENT_F | SEGMENT_G | SEGMENT_B | SEGMENT_C;
  const uint16_t NUM_5 = SEGMENT_A | SEGMENT_F | SEGMENT_G | SEGMENT_C | SEGMENT_D;
  const uint16_t NUM_6 = SEGMENT_A | SEGMENT_F | SEGMENT_E | SEGMENT_D | SEGMENT_C | SEGMENT_G;
  const uint16_t NUM_7 = SEGMENT_A | SEGMENT_B | SEGMENT_C;
  const uint16_t NUM_8 = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F | SEGMENT_G;
  const uint16_t NUM_9 = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_F | SEGMENT_G;
  const uint16_t LET_A = SEGMENT_E | SEGMENT_F | SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_G;
  const uint16_t LET_C = SEGMENT_A | SEGMENT_F | SEGMENT_E | SEGMENT_D;
  const uint16_t LET_D = SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_G;
  const uint16_t LET_E = SEGMENT_A | SEGMENT_F | SEGMENT_E | SEGMENT_D | SEGMENT_G;
  const uint16_t LET_F = SEGMENT_E | SEGMENT_F | SEGMENT_A | SEGMENT_G;
  const uint16_t LET_H = SEGMENT_B | SEGMENT_C | SEGMENT_E | SEGMENT_F | SEGMENT_G;
  const uint16_t LET_N = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_E | SEGMENT_F;
}

// bit mask of button
namespace FRAME_BUTTON
{
  const uint16_t POWER = 0x0400;
  const uint16_t FILTER = 0x0002;
  const uint16_t HEATER = 0x8000;
  const uint16_t BUBBLE = 0x0008;
  const uint16_t TEMP_UP = 0x1000;
  const uint16_t TEMP_DOWN = 0x0080;
  const uint16_t TEMP_UNIT = 0x2000;
  const uint16_t ALL = POWER | FILTER | HEATER | BUBBLE | TEMP_UP | TEMP_DOWN | TEMP_UNIT;
}

// frame type markers
namespace FRAME_TYPE
{
  const uint16_t CUE = 0x0100;
  const uint16_t LED = 0x4000;
  const uint16_t DIGIT = FRAME_DIGIT::POS_ALL;
  const uint16_t BUTTON = CUE | FRAME_BUTTON::ALL;
}

namespace DIGIT
{
  // 7-segment display update control
  const uint8_t POS_1 = 0x8;
  const uint8_t POS_2 = 0x4;
  const uint8_t POS_3 = 0x2;
  const uint8_t POS_4 = 0x1;
  const uint8_t POS_1_2 = POS_1 | POS_2;
  const uint8_t POS_1_2_3 = POS_1 | POS_2 | POS_3;
  const uint8_t POS_ALL = POS_1 | POS_2 | POS_3 | POS_4;

  // nibble value used to map a subset of non-numeric states of the 7-segment display
  const uint8_t LET_C = 0xC;
  const uint8_t LET_D = 0xD;
  const uint8_t LET_E = 0xE;
  const uint8_t LET_F = 0xF;

  const uint8_t LET_N = 0xA;
  const uint8_t OFF = 0xB;
};

namespace ERROR
{
  // internal binary value of error display (3 letters)
  const uint16_t NONE = 0;
  const uint16_t NO_WATER_FLOW = 0xE90;
  const uint16_t WATER_TEMP_LOW = 0xE94;
  const uint16_t WATER_TEMP_HIGH = 0xE95;
  const uint16_t SYSTEM = 0xE96;
  const uint16_t DRY_FIRE_PROTECT = 0xE97;
  const uint16_t TEMP_SENSOR = 0xE99;
  const uint16_t HEATING_ABORTED = 0xEAD; // A => DIGIT::LET_N ie: "END" => 0xEAD

  const uint16_t VALUES[] = {NO_WATER_FLOW, WATER_TEMP_LOW, WATER_TEMP_HIGH, WATER_TEMP_HIGH, SYSTEM, DRY_FIRE_PROTECT, TEMP_SENSOR, HEATING_ABORTED};
  const unsigned int COUNT = sizeof(VALUES) / sizeof(uint16_t);

  // human readable error on display
  const char CODE_90[] PROGMEM = "E90";
  const char CODE_94[] PROGMEM = "E94";
  const char CODE_95[] PROGMEM = "E95";
  const char CODE_96[] PROGMEM = "E96";
  const char CODE_97[] PROGMEM = "E97";
  const char CODE_99[] PROGMEM = "E99";
  const char CODE_END[] PROGMEM = "END";
  const char CODE_OTHER[] PROGMEM = "EXX";

  // English error messages
  const char EN_90[] PROGMEM = "no water flow";
  const char EN_94[] PROGMEM = "water temp too low";
  const char EN_95[] PROGMEM = "water temp too high";
  const char EN_96[] PROGMEM = "system error";
  const char EN_97[] PROGMEM = "dry fire protection";
  const char EN_99[] PROGMEM = "water temp sensor error";
  const char EN_END[] PROGMEM = "heating aborted after 72h";
  const char EN_OTHER[] PROGMEM = "error";

  // German error messages
  const char DE_90[] PROGMEM = "kein Wasserdurchfluss";
  const char DE_94[] PROGMEM = "Wassertemperatur zu niedrig";
  const char DE_95[] PROGMEM = "Wassertemperatur zu hoch";
  const char DE_96[] PROGMEM = "Systemfehler";
  const char DE_97[] PROGMEM = "Trocken-Brandschutz";
  const char DE_99[] PROGMEM = "Wassertemperatursensor defekt";
  const char DE_END[] PROGMEM = "Heizbetrieb nach 72 h deaktiviert";
  const char DE_OTHER[] PROGMEM = "Störung";

  const char *const TEXT[3][COUNT + 1] PROGMEM = {
      {CODE_90, CODE_94, CODE_95, CODE_96, CODE_97, CODE_99, CODE_END, CODE_OTHER},
      {EN_90, EN_94, EN_95, EN_96, EN_97, EN_99, EN_END, EN_OTHER},
      {DE_90, DE_94, DE_95, DE_96, DE_97, DE_99, DE_END, DE_OTHER}};
}

// special display values
inline uint16_t display2Num(uint16_t v) { return (((v >> 12) & 0x000F) * 100) + (((v >> 8) & 0x000F) * 10) + ((v >> 4) & 0x000F); }
inline uint16_t display2Error(uint16_t v) { return (v >> 4) & 0x0FFF; }
inline bool displayIsTemp(uint16_t v) { return (v & 0x000F) == DIGIT::LET_C || (v & 0x000F) == DIGIT::LET_F; }
inline bool displayIsError(uint16_t v) { return (v & 0xF000) == 0xE000; }
inline bool displayIsBlank(uint16_t v) { return (v & 0xFFF0) == ((DIGIT::OFF << 12) + (DIGIT::OFF << 8) + (DIGIT::OFF << 4)); }

// diagnostic mirrors of decodeDisplay's internal state (read by logDebug)
static volatile uint16_t g_dbgDisp = 0, g_dbgStable = 0, g_dbgStableTemp = 0;
static volatile uint32_t g_dbgChanges = 0; // how often the assembled value flips (instability)
static volatile uint32_t g_dbgReplies = 0; // button replies actually transmitted (DATA pulled low)

volatile SBH20IO::State SBH20IO::state;
volatile SBH20IO::Buttons SBH20IO::buttons;

// ESP32: configurable pins + precomputed masks (set in setup, read in ISRs)
volatile uint8_t SBH20IO::pinClock = 0;
volatile uint8_t SBH20IO::pinData = 0;
volatile uint8_t SBH20IO::pinLatch = 0;
volatile uint32_t SBH20IO::maskData = 0;
volatile uint32_t SBH20IO::maskLatch = 0;

// --- debug counters (debug build) ---
volatile uint32_t SBH20IO::dbgCue = 0;
volatile uint32_t SBH20IO::dbgDigit = 0;
volatile uint32_t SBH20IO::dbgLed = 0;
volatile uint32_t SBH20IO::dbgButton = 0;
volatile uint32_t SBH20IO::dbgOther = 0;
volatile uint16_t SBH20IO::dbgLastLed = 0;
volatile uint16_t SBH20IO::dbgLastDigit = 0;
volatile uint16_t SBH20IO::dbgLastButton = 0;

// deferred display-frame ring buffer (ISR producer, loop() consumer)
volatile uint16_t SBH20IO::frameBuf[SBH20IO::FRAME_BUF_SIZE];
volatile uint16_t SBH20IO::frameBufHead = 0;
volatile uint16_t SBH20IO::frameBufTail = 0;
volatile uint32_t SBH20IO::dbgIsrCalls = 0;
volatile uint32_t SBH20IO::dbgLatchCalls = 0;

// ESP32 fast GPIO helpers (require GPIO < 32)
inline bool SBH20IO::readData() { return (REG_READ(GPIO_IN_REG) & maskData) != 0; }
inline bool SBH20IO::readLatch() { return (REG_READ(GPIO_IN_REG) & maskLatch) != 0; }
inline void SBH20IO::driveDataLow()
{
  REG_WRITE(GPIO_OUT_W1TC_REG, maskData);    // ensure output latch is 0
  REG_WRITE(GPIO_ENABLE_W1TS_REG, maskData); // enable driver -> pulls line low
}
inline void SBH20IO::releaseData()
{
  REG_WRITE(GPIO_ENABLE_W1TC_REG, maskData); // disable driver -> input, pull-up high
}

// @TODO detect when latch signal stays low
void SBH20IO::setup(LANG language, uint8_t clockPin, uint8_t dataPin, uint8_t latchPin)
{
  this->language = language;

  pinClock = clockPin;
  pinData = dataPin;
  pinLatch = latchPin;
  maskData = (uint32_t)1 << dataPin;
  maskLatch = (uint32_t)1 << latchPin;

  // Configure all three signals as inputs so the fast GPIO_IN register reads work.
  // (A pin-role scan proved the data line read stuck until its input path was
  // enabled here; attachInterrupt only enables the clock + latch pins.) The data
  // line is open-drain with an external pull-up via the level shifter — we switch
  // it to an output only transiently to signal a button press.
  pinMode(pinClock, INPUT);
  pinMode(pinData, INPUT);
  pinMode(pinLatch, INPUT);

  // preset DATA output latch low so enabling the driver pulls the line low (TX)
  REG_WRITE(GPIO_OUT_W1TC_REG, maskData);

  // Start the polled receiver pinned to core 1, so it runs on APP_CPU while WiFi
  // runs on core 0 and cannot preempt the receive/transmit timing.
  xTaskCreatePinnedToCore(SBH20IO::spiTask, "sbh20_rx", 4096, nullptr, 12, nullptr, 1);
}

// ESP32 receive-setup task (pinned to core 1), with clock/data auto-detection.
//
// Runs once at boot on core 1 so the timing-critical capture stays isolated from WiFi (core
// 0). It first auto-detects which signal pin is the clock — the clock has far more
// transitions than data (the scan measured G19=6676 vs G22=905), so the YAML clock_pin /
// data_pin order doesn't matter — then installs the GPIO interrupts (clock-rising +
// latch-falling) that do the actual capture and deletes itself. A hardware interrupt latches
// every edge regardless of CPU load; the heavy 7-segment decode is deferred to the main loop
// via a ring buffer.
void SBH20IO::spiTask(void *arg)
{
  vTaskDelay(3000 / portTICK_PERIOD_MS); // let WiFi settle

  // --- auto-detect clock vs data: keep measuring until real bus activity appears ---
  for (;;)
  {
    const uint32_t mc = (uint32_t)1 << pinClock;
    const uint32_t md = (uint32_t)1 << pinData;
    uint32_t tc = 0, td = 0, iter = 0;
    uint32_t g = REG_READ(GPIO_IN_REG);
    uint32_t pc = g & mc, pd = g & md;
    int64_t t0 = esp_timer_get_time();
    for (;;)
    {
      g = REG_READ(GPIO_IN_REG);
      uint32_t cc = g & mc, cd = g & md;
      if (cc != pc) { tc++; pc = cc; }
      if (cd != pd) { td++; pd = cd; }
      if (((++iter) & 0x3FF) == 0 && (esp_timer_get_time() - t0) >= 30000) break; // 30 ms
    }
    if (tc > 50 || td > 50)
    {
      if (td > tc) // the "data" pin is busier -> it is really the clock; swap roles
      {
        uint8_t t = pinClock; pinClock = pinData; pinData = t;
        maskData = (uint32_t)1 << pinData;
        ESP_LOGW("sbh20", "auto-detect swapped clock<->data: clock=G%u data=G%u (trans c=%u d=%u)",
                 (unsigned) pinClock, (unsigned) pinData, td, tc);
      }
      else
      {
        ESP_LOGI("sbh20", "auto-detect: clock=G%u data=G%u (trans c=%u d=%u)",
                 (unsigned) pinClock, (unsigned) pinData, tc, td);
      }
      break;
    }
    ESP_LOGW("sbh20", "auto-detect: no bus activity yet (wake the panel) c=%u d=%u", tc, td);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  // Capture is interrupt-driven on this core (core 1). The earlier busy-poll was blind to
  // the clock for the ~2 us it spent running an inline decode, so it missed edges and ~half
  // the frames came out as garbage -- and button replies landed unreliably. A hardware GPIO
  // interrupt latches every edge no matter what the CPU is doing, so capture is clean (this
  // is exactly how the working D1-mini build behaves), no yield gaps are needed, and the
  // button reply (DATA low on the matching telegram, released on latch fall) is precise ->
  // reliable TX. The earlier interrupt attempt only failed because the pins were swapped,
  // which the auto-detect above now corrects.
  attachInterruptArg(digitalPinToInterrupt(pinLatch), SBH20IO::latchFallingISR, nullptr, FALLING);
  attachInterruptArg(digitalPinToInterrupt(pinClock), SBH20IO::clockRisingISR, nullptr, RISING);

  vTaskDelete(nullptr); // the ISRs handle capture/decode/TX from here; this task is done
}

void SBH20IO::processFrames()
{
  while (frameBufTail != frameBufHead)
  {
    uint16_t f = frameBuf[frameBufTail];
    frameBufTail = (uint16_t)((frameBufTail + 1) & FRAME_BUF_MASK);
    decodeDisplay(f);
  }
}

void SBH20IO::loop()
{
  processFrames();
  tickControls(); // pace out any queued button presses (non-blocking)

  // Online = still receiving frames from the panel. (The original keyed this off state
  // *changes*, so the device flapped to offline whenever the spa sat idle with a steady
  // display. Track the frame counter instead, so a steady-but-live bus stays online and
  // we only go offline when frames actually stop.)
  static unsigned int lastFrameCount = 0;
  unsigned long now = millis();
  if (state.stateUpdated || state.frameCounter != lastFrameCount)
  {
    lastFrameCount = state.frameCounter;
    lastStateUpdateTime = now;
    state.online = true;
    state.stateUpdated = false;
  }
  else if (timeDiff(now, lastStateUpdateTime) > CYCLE::RECEIVE_TIMEOUT)
  {
    state.online = false;
  }
}

bool SBH20IO::isOnline() const
{
  return state.online;
}

unsigned int SBH20IO::getTotalFrames() const
{
  return state.frameCounter;
}

unsigned int SBH20IO::getDroppedFrames() const
{
  return state.frameDropped;
}

int SBH20IO::getCurrentTemperature() const
{
  return (state.currentTemperature != UNDEF::USHORT) ? convertDisplayToCelsius(state.currentTemperature) : UNDEF::USHORT;
}

int SBH20IO::getTargetTemperature() const
{
  return (state.targetTemperature != UNDEF::USHORT) ? convertDisplayToCelsius(state.targetTemperature) : UNDEF::USHORT;
}

void SBH20IO::forceReadTargetTemperature()
{
  // Non-blocking setpoint read: queue a single "down" press and return immediately. The
  // ISR transmits it over the next button-poll telegrams and decodeDisplay captures the
  // setpoint the panel briefly reveals (the first down press only reveals it, it does not
  // change it). The old path BLOCKED the ESPHome
  // loop ~2 s waiting for a buzzer ACK -- that stalled the API and made Home Assistant
  // mark the whole device "unavailable"/unknown, and it repeated every 30 s while the
  // target was unknown. The caller's 30 s backoff is far longer than the panel's setpoint
  // display timeout, so each retry is a fresh reveal (never an accidental decrement).
  if (isPowerOn() == true && state.error == ERROR::NONE && !state.buzzer && buttons.toggleTempDown == 0)
  {
    buttons.toggleTempDown = BUTTON::PRESS_COUNT;
  }
}

unsigned int SBH20IO::getErrorValue() const
{
  return state.error;
}

String SBH20IO::getErrorMessage(unsigned int errorValue) const
{
  if (errorValue)
  {
    // get error text index of error value
    unsigned int i;
    for (i = 0; i < ERROR::COUNT; i++)
    {
      if (ERROR::VALUES[i] == errorValue)
      {
        break;
      }
    }

    // load error text from PROGMEM
    return FPSTR(ERROR::TEXT[(unsigned int)language][i]);
  }
  else
  {
    // no error
    return "";
  }
}

unsigned int SBH20IO::getRawLedValue() const
{
  return (state.ledStatus != UNDEF::USHORT) ? state.ledStatus : UNDEF::USHORT;
}

uint8_t SBH20IO::isPowerOn() const
{
  return (state.ledStatus != UNDEF::USHORT) ? ((state.ledStatus & FRAME_LED::POWER) != 0) : UNDEF::BOOL;
}

uint8_t SBH20IO::isFilterOn() const
{
  return (state.ledStatus != UNDEF::USHORT) ? ((state.ledStatus & FRAME_LED::FILTER) != 0) : UNDEF::BOOL;
}

uint8_t SBH20IO::isBubbleOn() const
{
  return (state.ledStatus != UNDEF::USHORT) ? ((state.ledStatus & FRAME_LED::BUBBLE) != 0) : UNDEF::BOOL;
}

uint8_t SBH20IO::isHeaterOn() const
{
  return (state.ledStatus != UNDEF::USHORT) ? ((state.ledStatus & (FRAME_LED::HEATER_ON | FRAME_LED::HEATER_STANDBY)) != 0) : UNDEF::BOOL;
}

uint8_t SBH20IO::isHeaterStandby() const
{
  return (state.ledStatus != UNDEF::USHORT) ? ((state.ledStatus & FRAME_LED::HEATER_STANDBY) != 0) : UNDEF::BOOL;
}

uint8_t SBH20IO::isBuzzerOn() const
{
  return (state.ledStatus != UNDEF::USHORT) ? ((state.ledStatus & FRAME_LED::NO_BEEP) == 0) : UNDEF::BOOL;
}

/**
 * request a new target water temperature (NON-BLOCKING)
 *
 * Records the request and returns immediately; tickControls() applies it as a closed loop.
 * The cached setpoint is invalidated so the sequencer reads a fresh value before acting,
 * and any in-flight temp press from a previous request is cancelled ("latest request wins").
 */
void SBH20IO::setTargetTemperature(int temp)
{
  if (temp < WATER_TEMP::SET_MIN || temp > WATER_TEMP::SET_MAX)
    return;
  if (isPowerOn() != true || state.error != ERROR::NONE)
    return;

  pendingTargetTemp = (uint16_t) temp;
  pendingTargetSetMs = millis();
  lastControlStepMs = pendingTargetSetMs;  // measure the read-timeout from the request start
  pendingTempDir = 0;
  pendingPressCount = 0;
  pendingAwaitingRead = false;
  buttons.toggleTempUp = 0;                // cancel any partial sequence from a prior request
  buttons.toggleTempDown = 0;
  state.targetTemperature = UNDEF::USHORT; // force a fresh reveal+read before computing the delta
}

/**
 * Non-blocking closed-loop control sequencer, called every loop().
 *
 * Drives a queued setpoint change one press at a time: read the panel's current setpoint,
 * press once toward the target, invalidate the cached reading, wait for the panel to
 * re-show (re-latch) the new setpoint, then read again and repeat until reached. Reading
 * after every press makes it self-correcting -- a press that only "reveals" the setpoint
 * (no change) is simply retried, and it can't overshoot -- and it never blocks the loop.
 * A press is "in flight" while its toggle counter is non-zero (the ISR decrements it as it
 * transmits) or while the spa is acking (buzzer).
 */
void SBH20IO::tickControls()
{
  if (pendingTargetTemp == UNDEF::USHORT)
    return; // idle

  unsigned long now = millis();

  // abort if the spa can no longer accept changes, after too many presses (safety cap), or
  // if the whole request has run too long
  if (isPowerOn() != true || state.error != ERROR::NONE ||
      pendingPressCount >= CONTROL_MAX_PRESSES || (now - pendingTargetSetMs) > 120000)
  {
    pendingTargetTemp = UNDEF::USHORT;
    pendingAwaitingRead = false;
    buttons.toggleTempUp = 0; // don't leave a press in flight once we declare idle
    buttons.toggleTempDown = 0;
    return;
  }

  // wait while a press is transmitting / being acked, and keep a settle gap between presses
  if (state.buzzer || buttons.toggleTempUp || buttons.toggleTempDown)
    return;
  if (now - lastControlStepMs < CONTROL_STEP_MS)
    return;

  int current = getTargetTemperature();

  // We invalidate the cached setpoint after every press, so an UNDEF reading means the panel
  // hasn't re-shown the (new) setpoint yet -- wait for it (it re-latches on each blink).
  if (current == (int) UNDEF::USHORT)
  {
    // No fresh reading yet. If a reveal/press has gone unanswered too long (e.g. a buzzer
    // ack cleared the toggle counter before it transmitted), drop the await so we re-issue
    // it. The press cap and overall timeout above bound this if the panel is truly dead.
    if (pendingAwaitingRead && (now - lastControlStepMs > CONTROL_READ_TIMEOUT_MS))
      pendingAwaitingRead = false;
    // need a reading: nudge a reveal (a down press). If the display happened to be open and
    // this changes the value, the feedback below corrects it. Don't stack reveals.
    if (!pendingAwaitingRead && buttons.toggleTempDown == 0)
    {
      buttons.toggleTempDown = BUTTON::PRESS_COUNT;
      pendingAwaitingRead = true;
      pendingPressCount++;
      lastControlStepMs = now;
    }
    return;
  }

  // fresh reading in hand
  pendingAwaitingRead = false;

  if (pendingTempDir == 0)
  {
    if (current == (int) pendingTargetTemp)
    {
      pendingTargetTemp = UNDEF::USHORT; // already at the target
      return;
    }
    pendingTempDir = (current < (int) pendingTargetTemp) ? 1 : -1;
  }

  bool reached = (pendingTempDir > 0) ? (current >= (int) pendingTargetTemp)
                                      : (current <= (int) pendingTargetTemp);
  if (reached)
  {
    pendingTargetTemp = UNDEF::USHORT; // done
    return;
  }

  // press once toward the target, then invalidate the cached setpoint so the next read is
  // the fresh post-press value (this is what makes it self-correcting)
  if (pendingTempDir > 0)
    buttons.toggleTempUp = BUTTON::PRESS_COUNT;
  else
    buttons.toggleTempDown = BUTTON::PRESS_COUNT;
  state.targetTemperature = UNDEF::USHORT;
  pendingAwaitingRead = true;
  pendingPressCount++;
  lastControlStepMs = now;
}

// All toggles are NON-BLOCKING: if the state needs to change and no press for that button
// is already in flight (and the spa isn't mid-ack/beep), queue one press and return. The
// ISR transmits it; the new LED state is picked up asynchronously by decodeLED.
void SBH20IO::setBubbleOn(bool on)
{
  if ((on ^ (isBubbleOn() == true)) && buttons.toggleBubble == 0 && !state.buzzer)
  {
    buttons.toggleBubble = BUTTON::PRESS_COUNT;
  }
}

void SBH20IO::setFilterOn(bool on)
{
  if ((on ^ (isFilterOn() == true)) && buttons.toggleFilter == 0 && !state.buzzer)
  {
    buttons.toggleFilter = BUTTON::PRESS_COUNT;
  }
}

void SBH20IO::setHeaterOn(bool on)
{
  if ((on ^ (isHeaterOn() == true || isHeaterStandby() == true)) && buttons.toggleHeater == 0 && !state.buzzer)
  {
    buttons.toggleHeater = BUTTON::PRESS_COUNT;
  }
}

void SBH20IO::setPowerOn(bool on)
{
  if ((on ^ (isPowerOn() == true)) && buttons.togglePower == 0 && !state.buzzer)
  {
    buttons.togglePower = BUTTON::PRESS_COUNT;
  }
}

uint16_t SBH20IO::convertDisplayToCelsius(uint16_t value) const
{
  uint16_t celsiusValue = display2Num(value);
  uint16_t tempUint = value & 0x000F;
  if (tempUint == DIGIT::LET_F)
  {
    // convert °F to °C
    float fValue = (float)celsiusValue;
    celsiusValue = (uint16_t)round(((fValue - 32) * 5) / 9);
  }
  else if (tempUint != DIGIT::LET_C)
  {
    celsiusValue = UNDEF::USHORT;
  }

  return (celsiusValue >= 0) && (celsiusValue <= 60) ? celsiusValue : UNDEF::USHORT;
}

void SBH20IO::latchFallingISR(void *arg)
{
  // release DATA after a button reply (was driven low in decodeButton)
  dbgLatchCalls++;
  releaseData();
}

void SBH20IO::clockRisingISR(void *arg)
{
  static uint16_t frame = 0x0000;
  static uint16_t receivedBits = 0x0000;
  dbgIsrCalls++;
  bool data = !readData();          // data bits are inverted
  bool enable = readLatch() == LOW; // latch low => frame in progress

  if (enable || receivedBits == (FRAME::BITS - 1))
  {
    frame = (frame << 1) + data;
    receivedBits++;

    if (receivedBits == FRAME::BITS)
    {
      state.frameCounter++;

      if (frame == FRAME_TYPE::CUE)
      {
        dbgCue++;
      }
      else if (frame & FRAME_TYPE::DIGIT)
      {
        dbgDigit++; dbgLastDigit = frame;
        // defer the heavier display decode to loop() via the ring buffer so this ISR
        // stays short; loopTask has ample CPU now that capture is interrupt-driven, so
        // the buffer drains without dropping frames
        uint16_t dnext = (uint16_t)((frameBufHead + 1) & FRAME_BUF_MASK);
        if (dnext != frameBufTail) { frameBuf[frameBufHead] = frame; frameBufHead = dnext; }
        else { state.frameDropped++; }
      }
      else if (frame & FRAME_TYPE::LED)
      {
        dbgLed++; dbgLastLed = frame;
        // ignore corrupt LED frames: a valid one only ever sets bits in the LED set
        // (marker 0x4000 | FILTER | BUBBLE | HEATER_STANDBY | NO_BEEP | HEATER_ON |
        // POWER = 0x5781). Garbage reads carry stray bits (e.g. 0x8000) and would
        // otherwise get "confirmed" and flip Power/Filter on their own.
        if (!(frame & ~((uint16_t)0x5781))) decodeLED(frame);
      }
      else if (frame & FRAME_TYPE::BUTTON)
      {
        dbgButton++; dbgLastButton = frame;
        decodeButton(frame);
      }
      else if (frame != 0)
      {
        dbgOther++;
      }

      receivedBits = 0;
    }
  }
  else
  {
    frame = 0;
    receivedBits = 0;
  }
}

void SBH20IO::logDebug()
{
  ESP_LOGV("sbh20dbg", "clkISR=%u latchISR=%u frames=%u | cue=%u dig=%u led=%u btn=%u oth=%u | lastLed=0x%04X lastDigit=0x%04X lastBtn=0x%04X",
           dbgIsrCalls, dbgLatchCalls, state.frameCounter,
           dbgCue, dbgDigit, dbgLed, dbgButton, dbgOther,
           dbgLastLed, dbgLastDigit, dbgLastButton);
  ESP_LOGV("sbh20dbg", "disp: assembled=0x%04X stable=0x%04X stableTemp=0x%04X changes=%u replies=%u | curTemp=0x%04X targetTemp=0x%04X",
           g_dbgDisp, g_dbgStable, g_dbgStableTemp, g_dbgChanges, g_dbgReplies, state.currentTemperature, state.targetTemperature);
  g_dbgChanges = 0;
  g_dbgReplies = 0;
  dbgCue = dbgDigit = dbgLed = dbgButton = dbgOther = 0;
}

inline uint8_t SBH20IO::BCD(uint16_t value)
{
  uint8_t digit;
  switch (value & FRAME_DIGIT::SEGMENTS)
  {
  case FRAME_DIGIT::OFF:
    digit = DIGIT::OFF;
    break;
  case FRAME_DIGIT::NUM_0:
    digit = 0x0;
    break;
  case FRAME_DIGIT::NUM_1:
    digit = 0x1;
    break;
  case FRAME_DIGIT::NUM_2:
    digit = 0x2;
    break;
  case FRAME_DIGIT::NUM_3:
    digit = 0x3;
    break;
  case FRAME_DIGIT::NUM_4:
    digit = 0x4;
    break;
  case FRAME_DIGIT::NUM_5:
    digit = 0x5;
    break;
  case FRAME_DIGIT::NUM_6:
    digit = 0x6;
    break;
  case FRAME_DIGIT::NUM_7:
    digit = 0x7;
    break;
  case FRAME_DIGIT::NUM_8:
    digit = 0x8;
    break;
  case FRAME_DIGIT::NUM_9:
    digit = 0x9;
    break;
  case FRAME_DIGIT::LET_C:
    digit = DIGIT::LET_C; // for °C
    break;
  case FRAME_DIGIT::LET_D:
    digit = DIGIT::LET_D; // for error code "END"
    break;
  case FRAME_DIGIT::LET_E:
    digit = DIGIT::LET_E; // for error code
    break;
  case FRAME_DIGIT::LET_F:
    digit = DIGIT::LET_F; // for °F
    break;
  case FRAME_DIGIT::LET_N:
  default:
    digit = DIGIT::LET_N; // for error code "END"
    break;
  }
  return digit;
}

inline void SBH20IO::decodeDisplay(uint16_t frame)
{
  static uint16_t value = 0;  // current display
  static uint16_t pValue = 0; // previous display
  static uint16_t stableValue = 0;
  static uint debounce = 0; // quick debounce

  static uint8_t largeDebounce = 0;    // larger than blank frames count
  static uint16_t stableTemp = 0x0000; // stable temperature

  uint8_t digit = BCD(frame);

  if (frame & FRAME_DIGIT::POS_1)
  {
    value = (value & 0x0FFF) | (digit << 12);
  }
  else if (frame & FRAME_DIGIT::POS_2)
  {
    value = (value & 0xF0FF) | (digit << 8);
  }
  else if (frame & FRAME_DIGIT::POS_3)
  {
    value = (value & 0xFF0F) | (digit << 4);
  }
  else if (frame & FRAME_DIGIT::POS_4)
  {
    value = (value & 0xFFF0) | digit;
    g_dbgDisp = value;

    // Reject garbage assemblies. The multiplexed display frequently yields scans whose
    // segments don't form valid characters (every digit decodes to LET_N -> 0xAAAA);
    // mixed in are the real readings (e.g. 0x103F = "103F"). Only let a plausible value
    // -- a temperature (C/F marker), a blank, or an error code -- reach the debounce, so
    // the garbage can't be "confirmed" as the temperature. Ignored scans don't disturb
    // the debounce chain, so the consistent real readings still converge.
    if (!(displayIsTemp(value) || displayIsBlank(value) || displayIsError(value)))
      return;

    if (value != pValue)
    {
      pValue = value;
      debounce = 3;
      g_dbgChanges++;
    }
    else
    {
      if (debounce)
        debounce--;
      else if (value != stableValue)
      {
        largeDebounce = 250;

        stableValue = value;
        g_dbgStable = stableValue;
        if (displayIsBlank(value))
        {
          if (state.targetTemperature != stableTemp)
          {
            state.targetTemperature = stableTemp;
            state.stateUpdated = true;
          }
        }
        else if (displayIsError(value))
        {
          state.error = display2Error(value);
        }
        else
        {
          stableTemp = stableValue;
          g_dbgStableTemp = stableTemp;
          // a normal temperature is on the display -> any prior error code has cleared
          if (state.error != ERROR::NONE)
          {
            state.error = ERROR::NONE;
            state.stateUpdated = true;
          }
        }
      }
      if (largeDebounce)
      {
        largeDebounce--;
      }
      else
      {
        if (state.currentTemperature != stableTemp)
        {
          state.currentTemperature = stableTemp;
          state.stateUpdated = true;
        }
      }
    }
  }
}

inline void SBH20IO::decodeLED(uint16_t frame)
{
  static uint16_t pFrame = 0x000;
  static int count = 0;

  if (frame == pFrame)
  {
    // wait for confirmation
    count--;
    if (count == 0)
    {
      if (state.ledStatus != frame)
      {
        state.ledStatus = frame;
        state.buzzer = !(state.ledStatus & FRAME_LED::NO_BEEP);
        state.stateUpdated = true;

        // clear buttons if buzzer is on
        if (state.buzzer)
        {
          buttons.toggleBubble = 0;
          buttons.toggleFilter = 0;
          buttons.toggleHeater = 0;
          buttons.togglePower = 0;
          buttons.toggleTempUp = 0;
          buttons.toggleTempDown = 0;
        }
      }
    }
  }
  else
  {
    // LED status changed -- require more identical reads to confirm than the ESP8266
    // original (3). The ESP32's polled capture has residual single-bit noise that can
    // briefly flip e.g. the FILTER bit; demanding a longer consistent run keeps those
    // transients from toggling the switches while still tracking real ~0.5 s blinks.
    pFrame = frame;
    count = 8;
  }
}

inline void SBH20IO::decodeButton(uint16_t frame)
{
  bool reply = false;
  if (frame & FRAME_BUTTON::FILTER)
  {
    if (buttons.toggleFilter)
    {
      reply = true;
      buttons.toggleFilter--;
    }
  }
  else if (frame & FRAME_BUTTON::HEATER)
  {
    if (buttons.toggleHeater)
    {
      reply = true;
      buttons.toggleHeater--;
    }
  }
  else if (frame & FRAME_BUTTON::BUBBLE)
  {
    if (buttons.toggleBubble)
    {
      reply = true;
      buttons.toggleBubble--;
    }
  }
  else if (frame & FRAME_BUTTON::POWER)
  {
    if (buttons.togglePower)
    {
      reply = true;
      buttons.togglePower--;
    }
  }
  else if (frame & FRAME_BUTTON::TEMP_UP)
  {
    if (buttons.toggleTempUp)
    {
      reply = true;
      buttons.toggleTempUp--;
    }
  }
  else if (frame & FRAME_BUTTON::TEMP_DOWN)
  {
    if (buttons.toggleTempDown)
    {
      reply = true;
      buttons.toggleTempDown--;
    }
  }

  if (reply)
  {
    // pull DATA low to signal the button press (released on latch falling edge)
    g_dbgReplies++;
    driveDataLow();
  }
}

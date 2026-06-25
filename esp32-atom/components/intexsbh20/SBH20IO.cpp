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
volatile uint32_t SBH20IO::dbgSpiTotal = 0;
volatile uint32_t SBH20IO::dbgSpiLen16 = 0;
volatile uint32_t SBH20IO::dbgSpiLenOther = 0;
volatile uint32_t SBH20IO::dbgLastLen = 0;
volatile uint16_t SBH20IO::dbgLastRaw = 0;
volatile uint32_t SBH20IO::dbgMaxLen = 0;
volatile uint16_t SBH20IO::dbgWord0 = 0;
volatile uint16_t SBH20IO::dbgWord1 = 0;

// (SPI-slave capture buffers removed — using the GPIO logic-analyzer probe below instead.)

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

// (legacy GPIO-ISR capture path retained below for reference/fallback; not used in the
//  SPI-slave build — setup() starts spiTask instead.)
void SBH20IO::installISRs(void *arg)
{
  attachInterruptArg(digitalPinToInterrupt(pinLatch), SBH20IO::latchFallingISR, arg, FALLING);
  attachInterruptArg(digitalPinToInterrupt(pinClock), SBH20IO::clockRisingISR, arg, RISING);
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

// ESP32 polled receiver (pinned to core 1), with clock/data auto-detection.
//
// The SB-H20 clock rings through the level shifter, so per-edge GPIO interrupts on the
// ESP32 are unreliable; instead we busy-poll on core 1 (WiFi runs on core 0) and run the
// proven per-edge decoder (clockRisingISR / latchFallingISR, unchanged from the D1-mini
// path) on each detected edge, in ~12 ms bursts with a ~4 ms yield so the ESPHome
// loopTask + idle task still get CPU and the task watchdog is fed.
//
// It first auto-detects which signal pin is the clock — the clock has far more
// transitions than data (the scan measured G19=6676 vs G22=905). The YAML clock_pin /
// data_pin order has been a recurring trap; this makes the receiver self-correcting
// regardless of how those are set.
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

  const uint32_t clk = (uint32_t)1 << pinClock;
  const uint32_t lat = (uint32_t)1 << pinLatch;

  for (;;)
  {
    int64_t t0 = esp_timer_get_time();
    uint32_t g = REG_READ(GPIO_IN_REG);
    uint32_t prevClk = g & clk;
    uint32_t prevLat = g & lat;
    uint32_t iter = 0;

    // tight capture burst: detect clock-rising + latch-falling edges and decode
    for (;;)
    {
      g = REG_READ(GPIO_IN_REG);
      uint32_t curClk = g & clk;
      uint32_t curLat = g & lat;
      if (curClk && !prevClk) clockRisingISR(nullptr);   // sample + accumulate one bit
      if (!curLat && prevLat) latchFallingISR(nullptr);  // release DATA after a button TX
      prevClk = curClk;
      prevLat = curLat;
      if (((++iter) & 0x3FF) == 0 && (esp_timer_get_time() - t0) >= 12000) break;
    }

    vTaskDelay(4 / portTICK_PERIOD_MS); // yield to loopTask + idle (feeds the WDT)
  }
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

  // device online check
  unsigned long now = millis();
  if (state.stateUpdated)
  {
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
  changeTargetTemperature(-1);
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
 * set desired water temperature by performing button up or down actions
 * repeatedly depending on temperature delta (blocking)
 */
void SBH20IO::setTargetTemperature(int temp)
{
  if (temp >= WATER_TEMP::SET_MIN && temp <= WATER_TEMP::SET_MAX)
  {
    if (isPowerOn() == true && state.error == ERROR::NONE)
    {
      // try to get initial temp
      int setTemp = getTargetTemperature();
      bool modifying = false;
      if (setTemp == UNDEF::USHORT)
      {
        // trigger temp modification
        changeTargetTemperature(-1);
        modifying = true;

        // wait for temp readback (will take 2-3 blink durations)
        int sleep = 20; // ms
        int tries = 4 * BLINK::PERIOD / sleep;
        do
        {
          delay(sleep);
          setTemp = getTargetTemperature();
          tries--;
        } while (setTemp == UNDEF::USHORT && tries);

        // check success
        if (setTemp == UNDEF::USHORT)
        {
          // error, abort
          DEBUG_MSG("\naborted\n");
          delay(1);
          return;
        }
      }

      // modify desired temp
      int deltaTemp = temp - setTemp;
      while (deltaTemp)
      {
        if (deltaTemp > 0)
        {
          changeTargetTemperature(1);
          if (modifying)
          {
            deltaTemp--;
            setTemp++;
          }
        }
        else
        {
          changeTargetTemperature(-1);
          if (modifying)
          {
            deltaTemp++;
            setTemp--;
          }
        }
        modifying = true;
      }
      delay(1);
    }
  }
}

/**
 * press specific button and wait for confirmation (blocking)
 */
bool SBH20IO::pressButton(volatile unsigned int &buttonPressCount)
{
  waitBuzzerOff();
  unsigned int tries = BUTTON::ACK_TIMEOUT / BUTTON::ACK_CHECK_PERIOD;
  buttonPressCount = BUTTON::PRESS_COUNT;
  while (buttonPressCount && tries)
  {
    delay(BUTTON::ACK_CHECK_PERIOD);
    tries--;
  }

  return tries;
}

void SBH20IO::setBubbleOn(bool on)
{
  if (on ^ (isBubbleOn() == true))
  {
    pressButton(buttons.toggleBubble);
  }
}

void SBH20IO::setFilterOn(bool on)
{
  if (on ^ (isFilterOn() == true))
  {
    pressButton(buttons.toggleFilter);
  }
}

void SBH20IO::setHeaterOn(bool on)
{
  if (on ^ (isHeaterOn() == true || isHeaterStandby() == true))
  {
    pressButton(buttons.toggleHeater);
  }
}

void SBH20IO::setPowerOn(bool on)
{
  bool active = isPowerOn() == true;
  if (on ^ active)
  {
    pressButton(buttons.togglePower);
  }
}

/**
 * wait for buzzer to go off or timeout and delay for a cycle period
 */
bool SBH20IO::waitBuzzerOff() const
{
  int tries = BUTTON::ACK_TIMEOUT / BUTTON::ACK_CHECK_PERIOD;
  while (state.buzzer && tries)
  {
    delay(BUTTON::ACK_CHECK_PERIOD);
    tries--;
  }

  // extra delay reduces chance to trigger auto repeat
  if (tries)
  {
    delay(2 * CYCLE::PERIOD);
    return true;
  }
  else
  {
    DEBUG_MSG("\nwBO fail");
    return false;
  }
}

/**
 * change water temperature setpoint by 1 degree and wait for confirmation (blocking)
 */
bool SBH20IO::changeTargetTemperature(int up)
{
  if (isPowerOn() == true && state.error == ERROR::NONE)
  {
    // perform button action
    waitBuzzerOff();
    int tries = BUTTON::ACK_TIMEOUT / BUTTON::ACK_CHECK_PERIOD;
    if (up > 0)
    {
      buttons.toggleTempUp = BUTTON::PRESS_COUNT;
      while (buttons.toggleTempUp && tries)
      {
        delay(BUTTON::ACK_CHECK_PERIOD);
        tries--;
      }
    }
    else if (up < 0)
    {
      buttons.toggleTempDown = BUTTON::PRESS_COUNT;
      while (buttons.toggleTempDown && tries)
      {
        delay(BUTTON::ACK_CHECK_PERIOD);
        tries--;
      }
    }

    if (tries && state.buzzer)
    {
      return true;
    }
    else
    {
      DEBUG_MSG("\ncWT fail");
      return false;
    }
  }
  return false;
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
        // defer heavy display decode to loop() so the ISR stays short
        uint16_t dnext = (uint16_t)((frameBufHead + 1) & FRAME_BUF_MASK);
        if (dnext != frameBufTail) { frameBuf[frameBufHead] = frame; frameBufHead = dnext; }
        else { state.frameDropped++; }
      }
      else if (frame & FRAME_TYPE::LED)
      {
        dbgLed++; dbgLastLed = frame;
        decodeLED(frame);
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
  ESP_LOGD("sbh20dbg", "clkISR=%u latchISR=%u frames=%u | cue=%u dig=%u led=%u btn=%u oth=%u | lastLed=0x%04X lastDigit=0x%04X lastBtn=0x%04X",
           dbgIsrCalls, dbgLatchCalls, state.frameCounter,
           dbgCue, dbgDigit, dbgLed, dbgButton, dbgOther,
           dbgLastLed, dbgLastDigit, dbgLastButton);
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
    if (value != pValue)
    {
      pValue = value;
      debounce = 3;
    }
    else
    {
      if (debounce)
        debounce--;
      else if (value != stableValue)
      {
        largeDebounce = 250;

        stableValue = value;
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
    // LED status changed
    pFrame = frame;
    count = 3;
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
    driveDataLow();
  }
}

/*
 * project:  Intex PureSpa SB-H20 WiFi Controller — ESP32 port
 *
 * file:     SBH20IO.h
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
 * ============================ ESP32 PORT NOTES ============================
 * The SB-H20 panel speaks a 16-bit, 100 kHz, SPI-mode-3-like serial protocol
 * (data sampled on clock rising edge, frame bounded by the latch line; data
 * bits inverted, MSB first). The only telegram we transmit is a button press:
 * the DATA line is pulled low for ~2 us after the latch goes high, once a
 * matching button telegram has been seen.
 *
 * This port differs from the ESP8266 original in three ways:
 *   1. Pins are configurable (passed into setup), not hard-coded.
 *   2. The ISRs use direct ESP32 GPIO registers (GPIO.in / GPIO.enable_w1ts /
 *      GPIO.enable_w1tc) instead of Arduino digitalRead()/pinMode(), which are
 *      too slow for the 2 us button window. (Requires the 3 pins to be GPIO < 32.)
 *   3. The GPIO ISR service is installed from a task pinned to core 1 so the
 *      timing-critical ISRs run on APP_CPU while WiFi runs on PRO_CPU (core 0).
 *      This is the key advantage over the ESP8266: WiFi can no longer preempt
 *      the receive/transmit timing.
 *
 * Keep ISRs in IRAM (IRAM_ATTR) and avoid heavy work / logging inside them.
 * =========================================================================
 */

#ifndef SBH20IO_H
#define SBH20IO_H

#include <WString.h>
#include <Arduino.h>
#include "common.h"

class SBH20IO
{
public:
  class UNDEF
  {
  public:
    static const uint8_t BOOL = 0xFF;
    static const uint16_t USHORT = 0xFFFF;
  };

  class WATER_TEMP
  {
  public:
    static const int SET_MIN = 20; // °C
    static const int SET_MAX = 40; // °C
  };

public:
  // ESP32: pins are supplied here (must be GPIO numbers < 32 for the fast ISR path)
  void setup(LANG language, uint8_t clockPin, uint8_t dataPin, uint8_t latchPin);
  void loop();

  // Installs the clock/latch ISRs on whichever core calls this. Invoked from a
  // task pinned to core 1 so the timing-critical ISRs run isolated from WiFi.
  static void installISRs(void *arg);

public:
  bool isOnline() const;

  int getCurrentTemperature() const;
  int getTargetTemperature() const;
  void forceReadTargetTemperature();

  uint8_t isBubbleOn() const;
  uint8_t isFilterOn() const;
  uint8_t isHeaterOn() const;
  uint8_t isHeaterStandby() const;
  uint8_t isPowerOn() const;
  uint8_t isBuzzerOn() const;

  void setTargetTemperature(int temp);

  void setBubbleOn(bool on);
  void setFilterOn(bool on);
  void setHeaterOn(bool on);
  void setPowerOn(bool on);

  unsigned int getErrorValue() const;
  String getErrorMessage(unsigned int errorValue) const;

  unsigned int getRawLedValue() const;

  unsigned int getTotalFrames() const;
  unsigned int getDroppedFrames() const;

private:
  class CYCLE
  {
  public:
    static const unsigned int TOTAL_FRAMES = 32;
    static const unsigned int DISPLAY_FRAMES = 5;
    static const unsigned int PERIOD = 21; // ms, period of frame cycle
    static const unsigned int RECEIVE_TIMEOUT = 50 * CYCLE::PERIOD; // ms
  };

  class FRAME
  {
  public:
    static const unsigned int BITS = 16;
    static const unsigned int FREQUENCY = CYCLE::TOTAL_FRAMES / CYCLE::PERIOD; // frames/ms
  };

  class BLINK
  {
  public:
    static const unsigned int PERIOD = 500; // ms
    static const unsigned int TEMP_FRAMES = PERIOD / 4 * FRAME::FREQUENCY;
    static const unsigned int STOPPED_FRAMES = 2 * PERIOD * FRAME::FREQUENCY;
  };

  class BUTTON
  {
  public:
    static const unsigned int PRESS_COUNT = BLINK::PERIOD / CYCLE::PERIOD - 2;
    static const unsigned int ACK_CHECK_PERIOD = 10; // ms
    static const unsigned int ACK_TIMEOUT = 2 * PRESS_COUNT * CYCLE::PERIOD; // ms
  };

private:
  struct State
  {
    uint16_t currentTemperature = UNDEF::USHORT;
    uint16_t targetTemperature = UNDEF::USHORT;
    uint16_t ledStatus = UNDEF::USHORT;

    bool buzzer = false;
    uint16_t error = 0;
    unsigned int lastErrorChangeFrameCounter = 0;

    bool online = true;
    bool stateUpdated = false;

    unsigned int frameCounter = 0;
    unsigned int frameDropped = 0;
  };

  struct Buttons
  {
    unsigned int toggleBubble = 0;
    unsigned int toggleFilter = 0;
    unsigned int toggleHeater = 0;
    unsigned int togglePower = 0;
    unsigned int toggleTempUp = 0;
    unsigned int toggleTempDown = 0;
  };

private:
  // ISR and ISR helper
  static void IRAM_ATTR latchFallingISR(void *arg);
  static void IRAM_ATTR clockRisingISR(void *arg);
  static inline uint8_t IRAM_ATTR BCD(uint16_t value);
  static inline void IRAM_ATTR decodeDisplay(uint16_t frame);
  static inline void IRAM_ATTR decodeLED(uint16_t frame);
  static inline void IRAM_ATTR decodeButton(uint16_t frame);

  // ESP32 fast GPIO helpers (pins < 32)
  static inline bool IRAM_ATTR readData();
  static inline bool IRAM_ATTR readLatch();
  static inline void IRAM_ATTR driveDataLow();
  static inline void IRAM_ATTR releaseData();

private:
  // ISR variables
  static volatile State state;
  static volatile Buttons buttons;

  // ESP32: pin numbers + precomputed bit masks (set in setup, read in ISRs)
  static volatile uint8_t pinClock;
  static volatile uint8_t pinData;
  static volatile uint8_t pinLatch;
  static volatile uint32_t maskData;
  static volatile uint32_t maskLatch;

private:
  uint16_t convertDisplayToCelsius(uint16_t value) const;
  bool waitBuzzerOff() const;
  bool pressButton(volatile unsigned int &buttonPressCount);
  bool changeTargetTemperature(int up);

private:
  LANG language;
  unsigned long lastStateUpdateTime = 0;
};

#endif /* SBH20IO_H */

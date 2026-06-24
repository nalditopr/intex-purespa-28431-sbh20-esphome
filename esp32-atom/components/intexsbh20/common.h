/*
 * project:  Intex PureSpa SB-H20 WiFi Controller — ESP32 port
 *
 * file:     common.h
 *
 * encoding: UTF-8
 * created:  13th March 2021
 *
 * Copyright (C) 2021 Jens B.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32 port note:
 * The ESP8266 hard-coded PIN namespace was removed. Pins are now supplied at
 * runtime via SBH20IO::setup() so they can be set from the ESPHome YAML, and
 * because the Atom Lite does not expose the ESP8266 default GPIOs.
 */

#ifndef COMMON_H
#define COMMON_H

#include <limits.h>

//#define SERIAL_DEBUG

// Languages
enum class LANG
{
  CODE = 0, EN = 1, DE = 2
};

// serial debugging
#ifdef SERIAL_DEBUG
#define DEBUG_MSG(...) Serial.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

// time delta with overflow support
static inline unsigned long timeDiff(unsigned long newTime, unsigned long oldTime)
{
  if (newTime >= oldTime)
  {
    return newTime - oldTime;
  }
  else
  {
    return ULONG_MAX - oldTime + newTime + 1;
  }
}

// unsigned int delta with overflow support
static inline unsigned long diff(unsigned int newVal, unsigned int oldVal)
{
  if (newVal >= oldVal)
  {
    return newVal - oldVal;
  }
  else
  {
    return UINT_MAX - oldVal + newVal + 1;
  }
}

#endif /* COMMON_H */

/*
 * Host-side unit tests for the SB-H20 display/frame decode math.
 *
 * These exercise the pure decoding logic (no ESP32 / hardware needed): the
 * 7-segment BCD mapping, the display->number / ->error / ->temperature
 * conversions, the blank/temp/error classifiers, and the frame-type masks.
 * The constants and functions below are copied verbatim from SBH20IO.cpp so a
 * regression in that logic is caught here.
 *
 * Build & run:
 *   g++ -std=c++17 -O2 -o test_decode test_decode.cpp && ./test_decode
 */
#include <cstdint>
#include <cstdio>
#include <cmath>

typedef unsigned int uint;

// ----- constants copied from SBH20IO.cpp -----
namespace FRAME_DIGIT {
  const uint16_t POS_1 = 0x0040, POS_2 = 0x0020, POS_3 = 0x0800, POS_4 = 0x0004;
  const uint16_t POS_ALL = POS_1 | POS_2 | POS_3 | POS_4;
  const uint16_t SEGMENT_A = 0x2000, SEGMENT_B = 0x1000, SEGMENT_C = 0x0200, SEGMENT_D = 0x0400;
  const uint16_t SEGMENT_E = 0x0080, SEGMENT_F = 0x0008, SEGMENT_G = 0x0010, SEGMENT_DP = 0x8000;
  const uint16_t SEGMENTS = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F | SEGMENT_G;
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
  const uint16_t LET_C = SEGMENT_A | SEGMENT_F | SEGMENT_E | SEGMENT_D;
  const uint16_t LET_D = SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_G;
  const uint16_t LET_E = SEGMENT_A | SEGMENT_F | SEGMENT_E | SEGMENT_D | SEGMENT_G;
  const uint16_t LET_F = SEGMENT_E | SEGMENT_F | SEGMENT_A | SEGMENT_G;
  const uint16_t LET_N = SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_E | SEGMENT_F;
}

namespace DIGIT {
  const uint8_t LET_C = 0xC, LET_D = 0xD, LET_E = 0xE, LET_F = 0xF, LET_N = 0xA, OFF = 0xB;
}

namespace FRAME_BUTTON {
  const uint16_t POWER = 0x0400, FILTER = 0x0002, HEATER = 0x8000, BUBBLE = 0x0008;
  const uint16_t TEMP_UP = 0x1000, TEMP_DOWN = 0x0080, TEMP_UNIT = 0x2000;
  const uint16_t ALL = POWER | FILTER | HEATER | BUBBLE | TEMP_UP | TEMP_DOWN | TEMP_UNIT;
}

namespace FRAME_TYPE {
  const uint16_t CUE = 0x0100;
  const uint16_t LED = 0x4000;
  const uint16_t DIGIT = FRAME_DIGIT::POS_ALL;
  const uint16_t BUTTON = CUE | FRAME_BUTTON::ALL;
}

const uint16_t UNDEF_USHORT = 0xFFFF;

inline uint16_t display2Num(uint16_t v) { return (((v >> 12) & 0x000F) * 100) + (((v >> 8) & 0x000F) * 10) + ((v >> 4) & 0x000F); }
inline uint16_t display2Error(uint16_t v) { return (v >> 4) & 0x0FFF; }
inline bool displayIsTemp(uint16_t v) { return (v & 0x000F) == DIGIT::LET_C || (v & 0x000F) == DIGIT::LET_F; }
inline bool displayIsError(uint16_t v) { return (v & 0xF000) == 0xE000; }
inline bool displayIsBlank(uint16_t v) { return (v & 0xFFF0) == ((DIGIT::OFF << 12) + (DIGIT::OFF << 8) + (DIGIT::OFF << 4)); }

uint8_t BCD(uint16_t value) {
  uint8_t digit;
  switch (value & FRAME_DIGIT::SEGMENTS) {
  case FRAME_DIGIT::OFF:   digit = DIGIT::OFF; break;
  case FRAME_DIGIT::NUM_0: digit = 0x0; break;
  case FRAME_DIGIT::NUM_1: digit = 0x1; break;
  case FRAME_DIGIT::NUM_2: digit = 0x2; break;
  case FRAME_DIGIT::NUM_3: digit = 0x3; break;
  case FRAME_DIGIT::NUM_4: digit = 0x4; break;
  case FRAME_DIGIT::NUM_5: digit = 0x5; break;
  case FRAME_DIGIT::NUM_6: digit = 0x6; break;
  case FRAME_DIGIT::NUM_7: digit = 0x7; break;
  case FRAME_DIGIT::NUM_8: digit = 0x8; break;
  case FRAME_DIGIT::NUM_9: digit = 0x9; break;
  case FRAME_DIGIT::LET_C: digit = DIGIT::LET_C; break;
  case FRAME_DIGIT::LET_D: digit = DIGIT::LET_D; break;
  case FRAME_DIGIT::LET_E: digit = DIGIT::LET_E; break;
  case FRAME_DIGIT::LET_F: digit = DIGIT::LET_F; break;
  case FRAME_DIGIT::LET_N:
  default:                 digit = DIGIT::LET_N; break;
  }
  return digit;
}

uint16_t convertDisplayToCelsius(uint16_t value) {
  uint16_t celsiusValue = display2Num(value);
  uint16_t tempUint = value & 0x000F;
  if (tempUint == DIGIT::LET_F) {
    float fValue = (float)celsiusValue;
    celsiusValue = (uint16_t)round(((fValue - 32) * 5) / 9);
  } else if (tempUint != DIGIT::LET_C) {
    celsiusValue = UNDEF_USHORT;
  }
  return (celsiusValue >= 0) && (celsiusValue <= 60) ? celsiusValue : UNDEF_USHORT;
}

// ----- tiny test harness -----
static int failures = 0;
static int checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

// build a display word from 3 digit nibbles + unit nibble
static uint16_t disp(uint8_t d1, uint8_t d2, uint8_t d3, uint8_t unit) {
  return (uint16_t)((d1 << 12) | (d2 << 8) | (d3 << 4) | unit);
}

int main() {
  // --- BCD: every 7-seg pattern maps to the right nibble ---
  CHECK(BCD(FRAME_DIGIT::OFF) == DIGIT::OFF);
  CHECK(BCD(FRAME_DIGIT::NUM_0) == 0x0);
  CHECK(BCD(FRAME_DIGIT::NUM_1) == 0x1);
  CHECK(BCD(FRAME_DIGIT::NUM_2) == 0x2);
  CHECK(BCD(FRAME_DIGIT::NUM_3) == 0x3);
  CHECK(BCD(FRAME_DIGIT::NUM_4) == 0x4);
  CHECK(BCD(FRAME_DIGIT::NUM_5) == 0x5);
  CHECK(BCD(FRAME_DIGIT::NUM_6) == 0x6);
  CHECK(BCD(FRAME_DIGIT::NUM_7) == 0x7);
  CHECK(BCD(FRAME_DIGIT::NUM_8) == 0x8);
  CHECK(BCD(FRAME_DIGIT::NUM_9) == 0x9);
  CHECK(BCD(FRAME_DIGIT::LET_C) == DIGIT::LET_C);
  CHECK(BCD(FRAME_DIGIT::LET_D) == DIGIT::LET_D);
  CHECK(BCD(FRAME_DIGIT::LET_E) == DIGIT::LET_E);
  CHECK(BCD(FRAME_DIGIT::LET_F) == DIGIT::LET_F);
  // DP/POS bits must be ignored by the segment mask
  CHECK(BCD(FRAME_DIGIT::NUM_5 | FRAME_DIGIT::SEGMENT_DP | FRAME_DIGIT::POS_1) == 0x5);

  // --- display2Num ---
  CHECK(display2Num(disp(0, 4, 0, DIGIT::LET_C)) == 40);
  CHECK(display2Num(disp(1, 0, 4, DIGIT::LET_F)) == 104);
  CHECK(display2Num(disp(0, 3, 8, DIGIT::LET_C)) == 38);

  // --- convertDisplayToCelsius ---
  CHECK(convertDisplayToCelsius(disp(0, 4, 0, DIGIT::LET_C)) == 40); // 40C
  CHECK(convertDisplayToCelsius(disp(0, 2, 0, DIGIT::LET_C)) == 20); // 20C
  CHECK(convertDisplayToCelsius(disp(1, 0, 4, DIGIT::LET_F)) == 40); // 104F -> 40C
  CHECK(convertDisplayToCelsius(disp(0, 6, 8, DIGIT::LET_F)) == 20); // 68F  -> 20C
  CHECK(convertDisplayToCelsius(disp(9, 9, 0, DIGIT::LET_C)) == UNDEF_USHORT); // 990 out of range
  CHECK(convertDisplayToCelsius(disp(0, 4, 0, 0x1)) == UNDEF_USHORT);          // unit not C/F

  // --- error classification ---
  CHECK(displayIsError(0xE904) == true);
  CHECK(display2Error(0xE904) == 0xE90);
  CHECK(displayIsError(0xE954) == true);
  CHECK(display2Error(0xE954) == 0xE95);
  CHECK(displayIsError(disp(0, 4, 0, DIGIT::LET_C)) == false);

  // --- blank classification ---
  CHECK(displayIsBlank(disp(DIGIT::OFF, DIGIT::OFF, DIGIT::OFF, 0x0)) == true);
  CHECK(displayIsBlank(disp(DIGIT::OFF, DIGIT::OFF, DIGIT::OFF, DIGIT::LET_C)) == true);
  CHECK(displayIsBlank(disp(0, 4, 0, DIGIT::LET_C)) == false);

  // --- temp unit classification ---
  CHECK(displayIsTemp(disp(0, 4, 0, DIGIT::LET_C)) == true);
  CHECK(displayIsTemp(disp(1, 0, 4, DIGIT::LET_F)) == true);
  CHECK(displayIsTemp(disp(0, 4, 0, 0x1)) == false);

  // --- frame-type masks (telegram classification) ---
  CHECK(FRAME_TYPE::CUE == 0x0100);
  CHECK(FRAME_TYPE::DIGIT == FRAME_DIGIT::POS_ALL);
  CHECK((FRAME_DIGIT::POS_1 & FRAME_TYPE::DIGIT) != 0); // a digit-select bit marks a DIGIT frame
  CHECK((0x4000 & FRAME_TYPE::LED) != 0);               // LED frame marker
  CHECK((FRAME_BUTTON::POWER & FRAME_TYPE::BUTTON) != 0);

  printf("\n%d checks, %d failures\n", checks, failures);
  if (failures == 0) printf("ALL TESTS PASSED\n");
  return failures ? 1 : 0;
}

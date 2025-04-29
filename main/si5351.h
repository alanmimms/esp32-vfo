#ifndef __SI5351_H__
#define __SI5351_H__

#include <stdint.h>

enum {
  SI5351CLK_2MA,
  SI5351CLK_4MA,
  SI5351CLK_6MA,
  SI5351CLK_8MA,
};


void SI5351Setup();

void SI5351SetDrive(uint8_t clkNumber, uint8_t drive);

// Pass f = 0 to disable the specified clock. Initially, all clocks
// are disabled.
void SI5351SetFreq(uint8_t clkNumber, uint32_t f);

// Set clk0 and clk1 frequency in quadrature to f01 Hz.
// Reverse I/Q if invertPhase.
void SI5351SetFreqQuadrature(uint32_t f01, bool invertPhase = false);

#endif // __SI5351_H__

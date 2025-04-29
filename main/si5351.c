#include <stdint.h>
#include <stdbool.h>
#include "si5351.h"

// Define ESP-IDF specific types and functions if not included elsewhere
// This is a placeholder - replace with your actual I2C implementation includes/defs
#ifndef ESP_IDF_I2C_TYPES
#define ESP_IDF_I2C_TYPES
#include "driver/i2c_master.h" // Assuming ESP-IDF I2C master driver
#include "esp_check.h"         // For ESP_ERROR_CHECK
#include "esp_log.h"           // For logging if needed

// Placeholder for the I2C device handle - ensure this is defined and initialized elsewhere
extern i2c_master_dev_handle_t i2cDevH;
#endif // ESP_IDF_I2C_TYPES


#define MHZ(N)	((N)*1000*1000UL) // Use UL suffix for large constants
#define KHZ(N)  ((N)*1000UL)

static const uint32_t xtalFreq = MHZ(25);
static const uint32_t vcoMax = MHZ(900);

// Total number of clocks in SI5351 we control.
enum {nClocks = 8};

// Register addresses (based on AN619 and common datasheets)
// Using enum for register addresses for debugger visibility and type safety
enum {
  REG_DEVICE_STATUS = 0,
  REG_INT_STATUS_STICKY = 1,
  REG_INT_STATUS_MASK = 2,
  REG_OUTPUT_DISABLE = 3,          // CLKx_OEB bits
  REG_OEB_PIN_DISABLE = 9,
  REG_PLL_INPUT_SOURCE = 15,        // PLLA_SRC, PLLB_SRC
  REG_CLK_CONTROL_BASE = 16,        // Base for CLK0_PDN, MSx_INT, MSx_SRC, CLKx_INV, CLKx_SRC, CLKx_IDRV
  REG_CLK0_CONTROL = 16,
  REG_CLK1_CONTROL = 17,
  REG_CLK2_CONTROL = 18,
  REG_CLK3_CONTROL = 19,
  REG_CLK4_CONTROL = 20,
  REG_CLK5_CONTROL = 21,
  REG_CLK6_CONTROL = 22,           // Also contains FBA_INT
  REG_CLK7_CONTROL = 23,           // Also contains FBB_INT
  REG_CLK0_3_DISABLE_STATE = 24,
  REG_CLK4_7_DISABLE_STATE = 25,
  REG_MSNA_BASE = 26,              // PLLA Feedback Multisynth registers (26..33)
  REG_MSNB_BASE = 34,              // PLLB Feedback Multisynth registers (34..41)
  REG_MS0_BASE = 42,               // Output Multisynth 0 registers (42..49)
  REG_MS1_BASE = 50,               // Output Multisynth 1 registers (50..57)
  REG_MS2_BASE = 58,               // Output Multisynth 2 registers (58..65)
  REG_MS3_BASE = 66,               // Output Multisynth 3 registers (66..73)
  REG_MS4_BASE = 74,               // Output Multisynth 4 registers (74..81)
  REG_MS5_BASE = 82,               // Output Multisynth 5 registers (82..89)
  REG_MS6_PARAM1 = 90,             // Output Multisynth 6 Parameter 1 (Integer divider)
  REG_MS7_PARAM1 = 91,             // Output Multisynth 7 Parameter 1 (Integer divider)
  REG_CLOCK6_7_DIVIDER = 92,       // R6_DIV, R7_DIV
  REG_SS_ENABLE = 149,             // SSC_EN
  REG_VCXO_PARAM_BASE = 162,       // 162..164
  REG_CLK_PHASE_OFFSET_BASE = 165, // CLKx_PHOFF (0..5), 165..170
  REG_PLL_RESET = 177,             // PLLA_RST, PLLB_RST
  REG_CRYSTAL_LOAD_CAP = 183,      // XTAL_CL
  REG_FANOUT_ENABLE = 187,         // CLKIN_FANOUT_EN, XO_FANOUT_EN, MS_FANOUT_EN
};

// --- Register Bit Definitions ---

// Register 3: Output Enable Control
// Macro still useful here as it depends on clock number 'n'
#define CLK_OEB(n) (1 << (n))

// Register 15: PLL Input Source
enum { // Anonymous enum for constants
  PLLA_SRC_XTAL   = (0 << 2),
  PLLA_SRC_CLKIN  = (1 << 2),
  PLLB_SRC_XTAL   = (0 << 3),
  PLLB_SRC_CLKIN  = (1 << 3),
  CLKIN_DIV_1     = (0 << 6),
  CLKIN_DIV_2     = (1 << 6),
  CLKIN_DIV_4     = (2 << 6),
  CLKIN_DIV_8     = (3 << 6)
}; // No typedef, no name needed

// Register 16-23: CLKx Control
#define CLK_IDRV_MASK   0x03
#define CLK_SRC_MASK    (0x03 << 2)
enum { // Anonymous enum for constants
  // Drive Strength (Bits 1:0)
  CLK_IDRV_2MA    = 0x00,
  CLK_IDRV_4MA    = 0x01,
  CLK_IDRV_6MA    = 0x02,
  CLK_IDRV_8MA    = 0x03,
  // Clock Source (Bits 3:2)
  CLK_SRC_XTAL    = (0x00 << 2),
  CLK_SRC_CLKIN   = (0x01 << 2),
  CLK_SRC_MS0     = (0x02 << 2), // Only for CLK1, CLK2, CLK3
  CLK_SRC_MS4     = (0x02 << 2), // Only for CLK5, CLK6, CLK7
  CLK_SRC_MSx     = (0x03 << 2), // Use Multisynth x (default)
  // Invert (Bit 4)
  CLK_INV         = (1 << 4),
  // Multisynth Source (Bit 5)
  MS_SRC_PLLA     = (0 << 5),
  MS_SRC_PLLB     = (1 << 5),
  // Integer Mode (Bit 6)
  MS_INT          = (1 << 6),    // Integer mode enable (MS0-MS5)
  FBA_INT         = (1 << 6),    // Integer mode for PLLA (Reg 22)
  FBB_INT         = (1 << 6),    // Integer mode for PLLB (Reg 23)
  // Power Down (Bit 7)
  CLK_PDN         = (1 << 7)
}; // No typedef, no name needed

// Register 44, 52, 60, 68, 76, 84: Multisynth x Parameters (P1[17:16], DIVBY4, R_DIV)
#define MS_P1_HIGH_MASK 0x03        // Mask for bits 1:0 (P1[17:16])
#define MS_DIVBY4_MASK  (0x03 << 2) // Mask for bits 3:2 (DIVBY4)
#define R_DIV_MASK      (0x07 << 4) // Mask for bits 6:4 (R_DIV)
#define R_DIV_SHIFT     4           // Shift amount for R_DIV field
enum { // Anonymous enum for constants
  // DIVBY4 Enable (Bits 3:2)
  MS_DIVBY4_ON    = (0x03 << 2),
  MS_DIVBY4_OFF   = (0x00 << 2), // Explicitly define off state if needed
  // R Divider (Bits 6:4) - Values include the shift
  R_DIV_1         = (0 << R_DIV_SHIFT),
  R_DIV_2         = (1 << R_DIV_SHIFT),
  R_DIV_4         = (2 << R_DIV_SHIFT),
  R_DIV_8         = (3 << R_DIV_SHIFT),
  R_DIV_16        = (4 << R_DIV_SHIFT),
  R_DIV_32        = (5 << R_DIV_SHIFT),
  R_DIV_64        = (6 << R_DIV_SHIFT),
  R_DIV_128       = (7 << R_DIV_SHIFT)
}; // No typedef, no name needed

// Register 177: PLL Reset
enum { // Anonymous enum for constants
  PLLA_RST_BIT    = (1 << 5),
  PLLB_RST_BIT    = (1 << 7)
}; // No typedef, no name needed

// Register 183: Crystal Load Capacitance
#define XTAL_CL_MASK    (0x03 << 6) // Mask for bits 7:6
enum { // Anonymous enum for constants
  XTAL_CL_6PF     = (1 << 6),
  XTAL_CL_8PF     = (2 << 6),
  XTAL_CL_10PF    = (3 << 6) // Default
}; // No typedef, no name needed

// Register 187: Fanout Enable
enum { // Anonymous enum for constants
  MS_FANOUT_EN_BIT    = (1 << 4),
  XO_FANOUT_EN_BIT    = (1 << 6),
  CLKIN_FANOUT_EN_BIT = (1 << 7)
}; // No typedef, no name needed


// --- Static Helper Functions ---

// Return log base 2 of n where n is a power of 2 from 1 to 128.
// Returns the R_DIV encoded value (already shifted).
static uint8_t getRDivEncoding(uint8_t n) {
  if (n == 0) return R_DIV_1; // Should not happen, treat as div by 1
  uint8_t rval = 0;
  while (n > 1) {
    n >>= 1;
    rval++;
  }
  // Return the shifted value directly as uint8_t
  return (rval << R_DIV_SHIFT);
}

// Write len bytes from bufP to consecutive registers starting at regBase.
static void writeRegs(uint8_t regBase, const uint8_t *bufP, uint8_t len) {

  if (!i2cDevH) {
    ESP_LOGE("SI5351", "I2C device handle not initialized");
    return;
  }

  i2c_master_transmit_multi_buffer_info_t mb[2] = {
    {.write_buffer = &regBase, .buffer_size = 1},
    {.write_buffer = (uint8_t *) bufP, .buffer_size = len},
  };

  ESP_ERROR_CHECK(i2c_master_multi_buffer_transmit(i2cDevH, mb, sizeof(mb)/sizeof(mb[0]), -1));
}

// Write a single byte 'value' to register 'regAddr'.
static void writeReg(uint8_t regAddr, uint8_t value) {
  writeRegs(regAddr, &value, 1);
}


// Read len bytes from consecutive registers starting at regBase into bufP.
static void readRegs(uint8_t regBase, uint8_t *bufP, uint8_t len) {

  if (!i2cDevH) {
    ESP_LOGE("SI5351", "I2C device handle not initialized");
    return;
  }

  uint8_t baseAddr = regBase; // Cast enum to uint8_t for transmission
  ESP_ERROR_CHECK(i2c_master_transmit_receive(i2cDevH, &baseAddr, 1, bufP, len, -1));
}

// Read a single byte from register 'regAddr'.
static uint8_t readReg(uint8_t regAddr) {
  uint8_t value;
  readRegs(regAddr, &value, 1);
  return value;
}


// Calculate and write Multisynth parameters P1, P2, P3 to registers.
// This function handles the encoding based on AN619 formulas.
// For integer mode (b=0, c=1), set p2=0, p3=1.
// For divide-by-4 mode, set p1=0, p2=0, p3=1 and ensure divBy4 is true.
// 'rDivEncoded' parameter is now uint8_t
static void setMSynthParams(uint8_t synthBase,
			    uint32_t p1, uint32_t p2, uint32_t p3,
			    uint8_t rDivEncoded, bool divBy4)
{
  uint8_t params[8] = {
    (p3 >> 8) & 0xFF,		// MSx_P3[15:8]
    p3 & 0xFF,			// MSx_P3[7:0]
    rDivEncoded |				 // R_DIV[2:0]
        ((p1 >> 16) & MS_P1_HIGH_MASK) |	 // MSx_DIVBY4[1:0]
        (divBy4 ? MS_DIVBY4_ON : MS_DIVBY4_OFF), // MSx_P1[17:16]
    (p1 >> 8) & 0xFF,		// MSx_P1[15:8]
    p1 & 0xFF,			// MSx_P1[7:0]
    ((p3 >> 12) & 0xF0) |			// MSx_P3[19:16]
        ((p2 >> 16) & 0x0F),			// MSx_P2[19:16]
    (p2 >> 8) & 0xFF,		// MSx_P2[15:8]
    p2 & 0xFF,			// MSx_P2[7:0]
  };

  writeRegs(synthBase, params, 8);
}

// Configure a Multisynth for fractional division a + b/c.
// Calculates P1, P2, P3 and calls setMSynthParams.
// 'rDivEncoded' parameter is now uint8_t
static void setMSynthFrac(uint8_t synthBase, uint32_t a, uint32_t b, uint32_t c, uint8_t rDivEncoded) {
  uint32_t p1, p2, p3;
  bool divBy4 = false; // Fractional mode cannot be divBy4

  if (b == 0) { // Treat as integer if b is 0
    p1 = 128 * a - 512;
    p2 = 0;
    p3 = 1;

    if (a == 4) { // Check for divBy4 case if integer is 4
      p1 = 0;
      divBy4 = true;
    }
  } else {
    // Ensure c is not zero to avoid division by zero
    if (c == 0) c = 1; // Or handle error appropriately
    uint64_t temp_b = 128ULL * b; // Use 64-bit intermediate for calculation
    uint32_t term = temp_b / c;
    p1 = 128 * a + term - 512;
    p2 = (uint32_t)(temp_b % c); // Remainder fits in 32 bits
    p3 = c;
  }

  // Ensure parameters are within valid range (P1: 18-bit, P2/P3: 20-bit)
  p1 &= 0x3FFFF;
  p2 &= 0xFFFFF;
  p3 &= 0xFFFFF;

  setMSynthParams(synthBase, p1, p2, p3, rDivEncoded, divBy4);
}

// Configure a Multisynth for integer division 'div'.
// Calculates P1, P2, P3 and calls setMSynthParams.
static void setMSynthInt(uint8_t synthBase, uint32_t div, uint8_t rDivEncoded) {
  uint32_t p1, p2, p3;
  bool divBy4 = false;

  p2 = 0;
  p3 = 1;

  if (div == 4) {
    p1 = 0; // Special case for DIVBY4
    divBy4 = true;
  } else {
    p1 = 128 * div - 512;
  }

  // Ensure parameters are within valid range
  p1 &= 0x3FFFF;

  setMSynthParams(synthBase, p1, p2, p3, rDivEncoded, divBy4);
}


// --- Internal API Functions ---

// Configure a PLL (PLLA or PLLB) to generate the target VCO frequency.
// Assumes PLL input is xtalFreq.
static void configurePLL(uint8_t pllBase, uint32_t vcoFreq) {
  // Calculate feedback divider ratio: vcoFreq = xtalFreq * (a + b/c)
  // a + b/c = vcoFreq / xtalFreq
  uint32_t a = vcoFreq / xtalFreq;
  // Calculate fractional part b/c = (vcoFreq % xtalFreq) / xtalFreq
  // Maximize precision using c = 1,048,575 (2^20 - 1)
  // b = floor(c * (vcoFreq % xtalFreq) / xtalFreq)
  uint64_t num = (uint64_t)(vcoFreq % xtalFreq) * 1048575ULL;
  uint32_t b = num / xtalFreq;
  uint32_t c = 1048575;

  // Determine control register and INT bit based on PLL
  uint8_t ctrlRegAddr = (pllBase == REG_MSNA_BASE) ? REG_CLK6_CONTROL : REG_CLK7_CONTROL;
  uint8_t intBit = (pllBase == REG_MSNA_BASE) ? FBA_INT : FBB_INT; // Use uint8_t for the bit value

  // Check if it's an integer division
  if (b == 0) {
    // Set FBA_INT/FBB_INT bit if 'a' is even
    uint8_t ctrlVal = readReg(ctrlRegAddr);
    if ((a % 2) == 0) {
      ctrlVal |= intBit;
    } else {
      ctrlVal &= ~intBit;
    }
    writeReg(ctrlRegAddr, ctrlVal);
    setMSynthInt(pllBase, a, R_DIV_1); // PLL rDiv is always 1
  } else {
    // Clear FBA_INT/FBB_INT bit for fractional mode
    uint8_t ctrlVal = readReg(ctrlRegAddr);
    ctrlVal &= ~intBit;
    writeReg(ctrlRegAddr, ctrlVal);
    setMSynthFrac(pllBase, a, b, c, R_DIV_1); // PLL rDiv is always 1
  }
}

// Configure an output stage (Multisynth, R divider, Clock Control)
// 'drive' parameter is now uint8_t
static void configureOutputSynth(uint8_t clkNumber, uint32_t outputFreq, uint32_t vcoFreq, bool usePLLA, uint8_t drive) {
  uint8_t msBase;
  uint8_t rDiv = 1;
  uint8_t rDivEncoded = R_DIV_1; // Use uint8_t
  uint8_t clkControlReg = REG_CLK_CONTROL_BASE + clkNumber;
  uint8_t clkCtrlVal = 0;
  bool isInteger = false;
  bool msIntEnable = false; // For MS0-MS5 integer mode bit

  // 1. Determine R divider if needed (for frequencies < 500 kHz)
  //    Adjust target frequency for Multisynth accordingly.
  if (outputFreq < KHZ(500) && outputFreq > 0) {
    // Calculate largest R divider that keeps MSynth freq >= 500kHz
    uint32_t msFreqTarget = vcoFreq / 2048; // Min MSynth output freq (VCO_min / Max_MS_Div)
    if (msFreqTarget < KHZ(500)) msFreqTarget = KHZ(500);

    rDiv = 1;
    while (rDiv < 128 && (outputFreq * rDiv * 2) < msFreqTarget) { // *2 for safety margin
      rDiv <<= 1;
    }
    // Check if required MSynth freq is too high
    if ((uint64_t)outputFreq * rDiv > MHZ(200)) {
      // Cannot achieve this low frequency, handle error?
      // For now, just use max R divider
      rDiv = 128;
    }
    outputFreq *= rDiv; // Adjust target frequency for the Multisynth stage
    rDivEncoded = getRDivEncoding(rDiv);
  }

  // 2. Determine Multisynth base address
  switch (clkNumber) {
  case 0: msBase = REG_MS0_BASE; break;
  case 1: msBase = REG_MS1_BASE; break;
  case 2: msBase = REG_MS2_BASE; break;
  case 3: msBase = REG_MS3_BASE; break;
  case 4: msBase = REG_MS4_BASE; break;
  case 5: msBase = REG_MS5_BASE; break;
    // MS6 and MS7 have special handling below
  case 6: msBase = REG_MS6_PARAM1; break; // Not a base, but the P1 register
  case 7: msBase = REG_MS7_PARAM1; break; // Not a base, but the P1 register
  default: return; // Invalid clock number
  }

  // 3. Calculate Multisynth divider parameters (a, b, c or just integer div)
  //    outputFreq = vcoFreq / (MS_Div * R_Div) => MS_Div = vcoFreq / (outputFreq * R_Div)
  //    Note: outputFreq was already multiplied by rDiv if rDiv > 1
  if (outputFreq == 0) return; // Avoid division by zero if adjusted outputFreq becomes 0
  uint64_t msDivx1M = (uint64_t)vcoFreq * 1000000ULL / outputFreq; // Calculate MS_Div * 1M for precision
  uint32_t a = msDivx1M / 1000000ULL;
  uint32_t b = 0, c = 1;

  // Check if the division is effectively integer
  if ((msDivx1M % 1000000ULL) < 1000) { // Allow small tolerance for calculation inaccuracies
    isInteger = true;
    b = 0;
    c = 1;
  } else {
    // Calculate fractional part for MS0-MS5
    if (clkNumber <= 5) {
      isInteger = false;
      // Maximize precision using c = 1,048,575
      uint64_t num = (msDivx1M % 1000000ULL) * 1048575ULL;
      b = num / 1000000ULL;
      c = 1048575;
    } else {
      // MS6/7 must be integer, round 'a'
      a = (msDivx1M + 500000ULL) / 1000000ULL; // Round to nearest integer
      isInteger = true;
      b = 0;
      c = 1;
    }
  }

  // 4. Handle MS6/MS7 specific constraints (even integers 6-254)
  if (clkNumber >= 6) {
    if (!isInteger) a = (msDivx1M + 500000ULL) / 1000000ULL; // Force integer if calc was fractional
    if (a < 6) a = 6;
    if (a > 254) a = 254;
    if ((a % 2) != 0) a++; // Force even by incrementing if odd
    if (a > 254) a = 254; // Check again after incrementing
    // Write directly to MS6_P1 or MS7_P1 register
    writeReg(msBase, a & 0xFF); // MS6/7 P1 is only 8 bits
  }
  // 5. Handle MS0-MS5
  else {
    // Check divider range (4, 6, 8, or 8+1/c to 2048)
    if (a < 4) a = 4; // Force minimum
    if (a > 2048) { a = 2048; b = 0; c = 1; isInteger = true; } // Force maximum

    if (a == 4 && b == 0) {
      // Use DIVBY4 mode
      setMSynthInt(msBase, 4, rDivEncoded);
      msIntEnable = true; // DIVBY4 requires integer mode bit
    } else if (a == 6 && b == 0) {
      setMSynthInt(msBase, 6, rDivEncoded);
      msIntEnable = true; // Enable integer mode bit if even integer
    } else if (a == 8 && b == 0) {
      setMSynthInt(msBase, 8, rDivEncoded);
      msIntEnable = true; // Enable integer mode bit if even integer
    } else if (isInteger) {
      // Generic integer > 8
      setMSynthInt(msBase, a, rDivEncoded);
      if ((a % 2) == 0) msIntEnable = true; // Enable integer mode bit if even
    } else {
      // Fractional (a must be >= 8)
      if (a < 8) { a = 8; b = 1; c = 1048575; } // Ensure a >= 8 for fractional
      setMSynthFrac(msBase, a, b, c, rDivEncoded);
      msIntEnable = false; // Fractional mode requires integer bit off
    }
  }

  // 6. Configure Clock Control Register
  clkCtrlVal = readReg(clkControlReg);
  // Clear fields first.
  // Note: MS_SRC_PLLA is 0, so clearing MS_SRC_PLLB sets it to PLLA.
  clkCtrlVal &= (uint8_t) ~(CLK_PDN | MS_INT | MS_SRC_PLLB | CLK_INV | CLK_SRC_MASK | CLK_IDRV_MASK);
  // Set fields
  clkCtrlVal |= (drive & CLK_IDRV_MASK);      // Drive strength (use uint8_t drive directly)
  clkCtrlVal |= CLK_SRC_MSx;                  // Source is Multisynth x
  if (!usePLLA) clkCtrlVal |= MS_SRC_PLLB;    // PLL source (default PLLA)
  if (msIntEnable && clkNumber <= 5) clkCtrlVal |= MS_INT; // Integer mode bit for MS0-5
  // clkCtrlVal &= ~CLK_INV; // Ensure not inverted (default)
  clkCtrlVal &= ~CLK_PDN;                     // Ensure powered ON

  writeReg(clkControlReg, clkCtrlVal);

  // 7. Enable the clock output driver
  uint8_t enableReg = readReg(REG_OUTPUT_DISABLE);
  enableReg &= ~CLK_OEB(clkNumber); // Clear OEB bit to enable output
  writeReg(REG_OUTPUT_DISABLE, enableReg);
}


// --- Public API Functions ---

// Initialize the Si5351 chip.
void SI5351Init() {
  // Wait for device status SYS_INIT to clear? (Optional but recommended)
  // while (readReg(REG_DEVICE_STATUS) & (1 << 7)) { vTaskDelay(pdMS_TO_TICKS(1)); }

  // Disable all clock outputs initially using OEB bits
  writeReg(REG_OUTPUT_DISABLE, 0xFF);

  // Power down all clock stages
  for (uint8_t clk = 0; clk < nClocks; ++clk) {
    writeReg(REG_CLK_CONTROL_BASE + clk, CLK_PDN); // Set only power down bit
  }

  // Set crystal load capacitance (e.g., 10pF)
  writeReg(REG_CRYSTAL_LOAD_CAP, XTAL_CL_10PF | 0x12); // Default 10pF, magic bits 010010b

  // Enable XO and MS fanout buffers (important!)
  writeReg(REG_FANOUT_ENABLE, XO_FANOUT_EN_BIT | MS_FANOUT_EN_BIT); // Enable XO and MS fanout

  // Reset both PLLs
  writeReg(REG_PLL_RESET, PLLA_RST_BIT | PLLB_RST_BIT);

  // Clear output disable register (outputs still off due to PDN bit)
  // Outputs will be enabled one by one in configureOutputSynth
  // writeReg(REG_OUTPUT_DISABLE, 0x00);
}

// Set the output drive strength for a specific clock.
// 'drive' parameter is now uint8_t, use enum constants like CLK_IDRV_8MA.
void SI5351SetDrive(uint8_t clkNumber, uint8_t drive) {
  if (clkNumber >= nClocks) return;
  uint8_t regAddr = REG_CLK_CONTROL_BASE + clkNumber;
  uint8_t value = readReg(regAddr);
  value &= ~CLK_IDRV_MASK; // Clear current drive strength
  value |= (drive & CLK_IDRV_MASK); // Set new drive strength
  writeReg(regAddr, value);
}


// Set the frequency for a specific clock output.
// Pass f = 0 to disable the specified clock.
void SI5351SetFreq(uint8_t clkNumber, uint32_t f) {
  if (clkNumber >= nClocks) return; // Check clock number validity

  // --- Disable Clock Case ---
  if (f == 0) {
    // Disable the clock output driver
    uint8_t enableReg = readReg(REG_OUTPUT_DISABLE);
    enableReg |= CLK_OEB(clkNumber); // Set OEB bit to disable output
    writeReg(REG_OUTPUT_DISABLE, enableReg);

    // Power down the clock stage
    uint8_t ctrlReg = REG_CLK_CONTROL_BASE + clkNumber;
    uint8_t ctrlVal = readReg(ctrlReg);
    ctrlVal |= CLK_PDN; // Set power down bit
    writeReg(ctrlReg, ctrlVal);
    return;
  }

  // --- Enable/Set Frequency Case ---

  // Validate frequency range (example: 8kHz to 160MHz, adjust as needed)
  // Note: Lower limit depends on R divider, upper limit on silicon version/datasheet
  if (f < KHZ(8) || f > MHZ(160)) {
    // Log error or handle invalid frequency
    // ESP_LOGE("SI5351", "Frequency %lu Hz out of range for CLK%d", f, clkNumber);
    return;
  }

  // --- Determine PLL and Output Synth Parameters ---
  // This section implements a basic frequency planning strategy.
  // It aims for a high VCO frequency (e.g., 900 MHz) for better jitter,
  // but adjustments might be needed based on specific requirements or
  // if multiple clocks share a PLL.

  uint32_t vcoFreq = vcoMax;	// Target VCO frequency (fixed for simplicity)
  bool usePLLA = true;		// Use PLLA for this clock (fixed for simplicity)
  uint8_t pllBase = usePLLA ? REG_MSNA_BASE : REG_MSNB_BASE;
  uint8_t drive = CLK_IDRV_8MA; // Default drive strength (use uint8_t)

  // TODO: Implement more sophisticated frequency planning if needed,
  // e.g., choosing VCO based on output freq for integer divides,
  // managing shared PLLs, etc.

  // --- Configure PLL and Output Stage using Internal API ---

  // 1. Configure the selected PLL
  configurePLL(pllBase, vcoFreq);

  // 2. Configure the output stage (Multisynth, R divider, Clock Control)
  //    The output frequency 'f' passed here is the final desired frequency.
  //    configureOutputSynth will handle R dividers internally if f < 500kHz.
  configureOutputSynth(clkNumber, f, vcoFreq, usePLLA, drive);

}

// Set clk0 and clk1 frequency in quadrature to f01 Hz.
// Reverse I/Q if invertPhase.
void SI5351SetFreqQuadrature(uint32_t f01, bool invertPhase) {
  if (f01 == 0) {
    SI5351SetFreq(0, 0);
    SI5351SetFreq(1, 0);
    return;
  }

  // Validate frequency range
  if (f01 < KHZ(8) || f01 > MHZ(160)) {
    // ESP_LOGE("SI5351", "Quadrature frequency %lu Hz out of range", f01);
    return;
  }

  // --- Frequency Planning ---
  uint32_t vcoFreq = vcoMax;	// Use a fixed VCO for simplicity
  bool usePLLA = true;		// Use PLLA for both clocks
  uint8_t pllBase = REG_MSNA_BASE;
  uint8_t drive = CLK_IDRV_8MA; // Use uint8_t

  // --- Configure PLL ---
  configurePLL(pllBase, vcoFreq);

  // --- Configure Output Stages ---
  // Calculate base output divider (integer part) for phase offset calculation
  if (f01 == 0) return; // Avoid division by zero
  uint32_t msDiv = vcoFreq / f01; // Integer division is sufficient for phase offset calc

  // Configure CLK0 (I channel)
  configureOutputSynth(0, f01, vcoFreq, usePLLA, drive);

  // Configure CLK1 (Q channel)
  configureOutputSynth(1, f01, vcoFreq, usePLLA, drive);

  // --- Set Phase Offset ---
  // Register value CLKx_PHOFF = round( vcoFreq / f_output ) = round(MS_Div)
  uint8_t phaseOffsetRegVal = msDiv & 0x7F; // Use integer part of MS_Div, limit to 7 bits (register field size)

  const uint8_t r0 = REG_CLK_PHASE_OFFSET_BASE + 0;
  const uint8_t r1 = REG_CLK_PHASE_OFFSET_BASE + 1;

  if (invertPhase) {
    // Set CLK0 offset, CLK1 zero
    writeReg(r0, phaseOffsetRegVal);
    writeReg(r1, 0);
  } else {
    // Set CLK1 offset, CLK0 zero
    writeReg(r0, 0);
    writeReg(r1, phaseOffsetRegVal);
  }

  // IMPORTANT: Phase offset requires MSx_INT = 0 (fractional mode)
  // Ensure integer mode is disabled for CLK0 and CLK1 if phase offset is used.
  uint8_t ctrl0 = readReg(REG_CLK0_CONTROL);
  uint8_t ctrl1 = readReg(REG_CLK1_CONTROL);
  ctrl0 &= ~MS_INT; // Clear integer mode bit
  ctrl1 &= ~MS_INT; // Clear integer mode bit
  writeReg(REG_CLK0_CONTROL, ctrl0);
  writeReg(REG_CLK1_CONTROL, ctrl1);
}

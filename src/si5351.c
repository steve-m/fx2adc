/*
 * Si5351 PLL driver, taken from
 * https://github.com/afiskon/stm32-si5351/tree/main/si5351
 *
 * "This library was forked from ProjectsByJRP/si5351-stm32 which
 * in it's turn is a port of adafruit/Adafruit_Si5351_Library.
 * Both libraries are licensed under BSD."
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Original license text below:
 *
 * Copyright (c) 2014, Adafruit Industries
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the
 * names of its contributors may be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <si5351.h>
#include <fx2adc_i2c.h>
#define SI5351_ADDRESS 0x60

void *fx2adc_dev = NULL;

// Writes an 8 bit value of a register over I2C.
void si5351_write(uint8_t reg, uint8_t value)
{
	uint8_t buf[2];
	buf[0] = reg;
	buf[1] = value;

	fx2adc_i2c_write_fn(fx2adc_dev, SI5351_ADDRESS, buf, sizeof(buf));
}

// Common code for _SetupPLL and _SetupOutput
void si5351_writeBulk(uint8_t baseaddr, int32_t P1, int32_t P2, int32_t P3, uint8_t divBy4, si5351RDiv_t rdiv)
{
	uint8_t buf[9] = { baseaddr,
			   (P3 >> 8) & 0xff,
			   P3 & 0xff,
			   ((P1 >> 16) & 0x3) | ((divBy4 & 0x3) << 2) | ((rdiv & 0x7) << 4),
			   (P1 >> 8) & 0xff,
			   P1 & 0xff,
			   ((P3 >> 12) & 0xf0) | ((P2 >> 16) & 0xf),
			   (P2 >> 8) & 0xff,
			   P2 & 0xff };

	fx2adc_i2c_write_fn(fx2adc_dev, SI5351_ADDRESS, buf, sizeof(buf));
}

int si5351_read(uint8_t reg, uint8_t *value)
{
	int r = fx2adc_i2c_write_fn(fx2adc_dev, SI5351_ADDRESS, &reg, 1);
	if (r < 0)
		return -1;

	r = fx2adc_i2c_read_fn(fx2adc_dev, SI5351_ADDRESS, value, 1);
	if (r != 1)
		return -1;

	return 0;
}

// See http://www.silabs.com/Support%20Documents/TechnicalDocs/AN619.pdf
enum {
	SI5351_REGISTER_0_DEVICE_STATUS				= 0,
	SI5351_REGISTER_1_INTERRUPT_STATUS_STICKY		= 1,
	SI5351_REGISTER_2_INTERRUPT_STATUS_MASK			= 2,
	SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL			= 3,
	SI5351_REGISTER_9_OEB_PIN_ENABLE_CONTROL		= 9,
	SI5351_REGISTER_15_PLL_INPUT_SOURCE			= 15,
	SI5351_REGISTER_16_CLK0_CONTROL				= 16,
	SI5351_REGISTER_17_CLK1_CONTROL				= 17,
	SI5351_REGISTER_18_CLK2_CONTROL				= 18,
	SI5351_REGISTER_19_CLK3_CONTROL				= 19,
	SI5351_REGISTER_20_CLK4_CONTROL				= 20,
	SI5351_REGISTER_21_CLK5_CONTROL				= 21,
	SI5351_REGISTER_22_CLK6_CONTROL				= 22,
	SI5351_REGISTER_23_CLK7_CONTROL				= 23,
	SI5351_REGISTER_24_CLK3_0_DISABLE_STATE			= 24,
	SI5351_REGISTER_25_CLK7_4_DISABLE_STATE			= 25,
	SI5351_REGISTER_42_MULTISYNTH0_PARAMETERS_1		= 42,
	SI5351_REGISTER_43_MULTISYNTH0_PARAMETERS_2		= 43,
	SI5351_REGISTER_44_MULTISYNTH0_PARAMETERS_3		= 44,
	SI5351_REGISTER_45_MULTISYNTH0_PARAMETERS_4		= 45,
	SI5351_REGISTER_46_MULTISYNTH0_PARAMETERS_5		= 46,
	SI5351_REGISTER_47_MULTISYNTH0_PARAMETERS_6		= 47,
	SI5351_REGISTER_48_MULTISYNTH0_PARAMETERS_7		= 48,
	SI5351_REGISTER_49_MULTISYNTH0_PARAMETERS_8		= 49,
	SI5351_REGISTER_50_MULTISYNTH1_PARAMETERS_1		= 50,
	SI5351_REGISTER_51_MULTISYNTH1_PARAMETERS_2		= 51,
	SI5351_REGISTER_52_MULTISYNTH1_PARAMETERS_3		= 52,
	SI5351_REGISTER_53_MULTISYNTH1_PARAMETERS_4		= 53,
	SI5351_REGISTER_54_MULTISYNTH1_PARAMETERS_5		= 54,
	SI5351_REGISTER_55_MULTISYNTH1_PARAMETERS_6		= 55,
	SI5351_REGISTER_56_MULTISYNTH1_PARAMETERS_7		= 56,
	SI5351_REGISTER_57_MULTISYNTH1_PARAMETERS_8		= 57,
	SI5351_REGISTER_58_MULTISYNTH2_PARAMETERS_1		= 58,
	SI5351_REGISTER_59_MULTISYNTH2_PARAMETERS_2		= 59,
	SI5351_REGISTER_60_MULTISYNTH2_PARAMETERS_3		= 60,
	SI5351_REGISTER_61_MULTISYNTH2_PARAMETERS_4		= 61,
	SI5351_REGISTER_62_MULTISYNTH2_PARAMETERS_5		= 62,
	SI5351_REGISTER_63_MULTISYNTH2_PARAMETERS_6		= 63,
	SI5351_REGISTER_64_MULTISYNTH2_PARAMETERS_7		= 64,
	SI5351_REGISTER_65_MULTISYNTH2_PARAMETERS_8		= 65,
	SI5351_REGISTER_66_MULTISYNTH3_PARAMETERS_1		= 66,
	SI5351_REGISTER_67_MULTISYNTH3_PARAMETERS_2		= 67,
	SI5351_REGISTER_68_MULTISYNTH3_PARAMETERS_3		= 68,
	SI5351_REGISTER_69_MULTISYNTH3_PARAMETERS_4		= 69,
	SI5351_REGISTER_70_MULTISYNTH3_PARAMETERS_5		= 70,
	SI5351_REGISTER_71_MULTISYNTH3_PARAMETERS_6		= 71,
	SI5351_REGISTER_72_MULTISYNTH3_PARAMETERS_7		= 72,
	SI5351_REGISTER_73_MULTISYNTH3_PARAMETERS_8		= 73,
	SI5351_REGISTER_74_MULTISYNTH4_PARAMETERS_1		= 74,
	SI5351_REGISTER_75_MULTISYNTH4_PARAMETERS_2		= 75,
	SI5351_REGISTER_76_MULTISYNTH4_PARAMETERS_3		= 76,
	SI5351_REGISTER_77_MULTISYNTH4_PARAMETERS_4		= 77,
	SI5351_REGISTER_78_MULTISYNTH4_PARAMETERS_5		= 78,
	SI5351_REGISTER_79_MULTISYNTH4_PARAMETERS_6		= 79,
	SI5351_REGISTER_80_MULTISYNTH4_PARAMETERS_7		= 80,
	SI5351_REGISTER_81_MULTISYNTH4_PARAMETERS_8		= 81,
	SI5351_REGISTER_82_MULTISYNTH5_PARAMETERS_1		= 82,
	SI5351_REGISTER_83_MULTISYNTH5_PARAMETERS_2		= 83,
	SI5351_REGISTER_84_MULTISYNTH5_PARAMETERS_3		= 84,
	SI5351_REGISTER_85_MULTISYNTH5_PARAMETERS_4		= 85,
	SI5351_REGISTER_86_MULTISYNTH5_PARAMETERS_5		= 86,
	SI5351_REGISTER_87_MULTISYNTH5_PARAMETERS_6		= 87,
	SI5351_REGISTER_88_MULTISYNTH5_PARAMETERS_7		= 88,
	SI5351_REGISTER_89_MULTISYNTH5_PARAMETERS_8		= 89,
	SI5351_REGISTER_90_MULTISYNTH6_PARAMETERS		= 90,
	SI5351_REGISTER_91_MULTISYNTH7_PARAMETERS		= 91,
	SI5351_REGISTER_92_CLOCK_6_7_OUTPUT_DIVIDER		= 92,
	SI5351_REGISTER_165_CLK0_INITIAL_PHASE_OFFSET		= 165,
	SI5351_REGISTER_166_CLK1_INITIAL_PHASE_OFFSET		= 166,
	SI5351_REGISTER_167_CLK2_INITIAL_PHASE_OFFSET		= 167,
	SI5351_REGISTER_168_CLK3_INITIAL_PHASE_OFFSET		= 168,
	SI5351_REGISTER_169_CLK4_INITIAL_PHASE_OFFSET		= 169,
	SI5351_REGISTER_170_CLK5_INITIAL_PHASE_OFFSET		= 170,
	SI5351_REGISTER_177_PLL_RESET				= 177,
	SI5351_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE	= 183
};

typedef enum {
	SI5351_CRYSTAL_LOAD_6PF  = (1<<6),
	SI5351_CRYSTAL_LOAD_8PF  = (2<<6),
	SI5351_CRYSTAL_LOAD_10PF = (3<<6)
} si5351CrystalLoad_t;

int32_t si5351Correction;

/*
 * Initializes Si5351. Call this function before doing anything else.
 * `Correction` is the difference of actual frequency and desired frequency @ 100 MHz.
 * It can be measured at lower frequencies and scaled linearly.
 * E.g. if you get 10_000_097 Hz instead of 10_000_000 Hz, `correction` is 97*10 = 970
 */
int si5351_Init(void *dev, int32_t correction)
{
	uint8_t val;
	fx2adc_dev = dev;
	si5351Correction = correction;

	/* check if chip is present */
	if (si5351_read(SI5351_REGISTER_0_DEVICE_STATUS, &val) < 0)
		return -1;

	// Disable all outputs by setting CLKx_DIS high
	si5351_write(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, 0xFF);

	// Power down all output drivers
	si5351_write(SI5351_REGISTER_16_CLK0_CONTROL, 0x80);
	si5351_write(SI5351_REGISTER_17_CLK1_CONTROL, 0x80);
	si5351_write(SI5351_REGISTER_18_CLK2_CONTROL, 0x80);
	si5351_write(SI5351_REGISTER_19_CLK3_CONTROL, 0x80);
	si5351_write(SI5351_REGISTER_20_CLK4_CONTROL, 0x80);
	si5351_write(SI5351_REGISTER_21_CLK5_CONTROL, 0x80);
	si5351_write(SI5351_REGISTER_22_CLK6_CONTROL, 0x80);
	si5351_write(SI5351_REGISTER_23_CLK7_CONTROL, 0x80);

	// Set the load capacitance for the XTAL
	si5351CrystalLoad_t crystalLoad = SI5351_CRYSTAL_LOAD_10PF;
	si5351_write(SI5351_REGISTER_183_CRYSTAL_INTERNAL_LOAD_CAPACITANCE, crystalLoad);

	return 0;
}

// Sets the multiplier for given PLL
void si5351_SetupPLL(si5351PLL_t pll, si5351PLLConfig_t* conf)
{
	int32_t P1, P2, P3;
	int32_t mult = conf->mult;
	int32_t num = conf->num;
	int32_t denom = conf->denom;

	P1 = 128 * mult + (128 * num)/denom - 512;
	// P2 = 128 * num - denom * ((128 * num)/denom);
	P2 = (128 * num) % denom;
	P3 = denom;

	// Get the appropriate base address for the PLL registers
	uint8_t baseaddr = (pll == SI5351_PLL_A ? 26 : 34);
	si5351_writeBulk(baseaddr, P1, P2, P3, 0, 0);

	// Reset both PLLs
	si5351_write(SI5351_REGISTER_177_PLL_RESET, (1<<7) | (1<<5) );
}

// Configures PLL source, drive strength, multisynth divider, Rdivider and phaseOffset.
// Returns 0 on success, != 0 otherwise.
int si5351_SetupOutput(uint8_t output,
		       si5351PLL_t pllSource,
		       si5351DriveStrength_t driveStrength,
		       si5351OutputConfig_t* conf,
		       uint8_t phaseOffset)
{
	int32_t div = conf->div;
	int32_t num = conf->num;
	int32_t denom = conf->denom;
	uint8_t divBy4 = 0;
	int32_t P1, P2, P3;

	if(output > 2) {
		return 1;
	}

	if((!conf->allowIntegerMode) && ((div < 8) || ((div == 8) && (num == 0)))) {
		// div in { 4, 6, 8 } is possible only in integer mode
		return 2;
	}

	if(div == 4) {
		// special DIVBY4 case, see AN619 4.1.3
		P1 = 0;
		P2 = 0;
		P3 = 1;
		divBy4 = 0x3;
	} else {
		P1 = 128 * div + ((128 * num)/denom) - 512;
		// P2 = 128 * num - denom * (128 * num)/denom;
		P2 = (128 * num) % denom;
		P3 = denom;
	}

	// Get the register addresses for given channel
	uint8_t baseaddr = 0;
	uint8_t phaseOffsetRegister = 0;
	uint8_t clkControlRegister = 0;
	switch (output) {
	case 0:
		baseaddr = SI5351_REGISTER_42_MULTISYNTH0_PARAMETERS_1;
		phaseOffsetRegister = SI5351_REGISTER_165_CLK0_INITIAL_PHASE_OFFSET;
		clkControlRegister = SI5351_REGISTER_16_CLK0_CONTROL;
		break;
	case 1:
		baseaddr = SI5351_REGISTER_50_MULTISYNTH1_PARAMETERS_1;
		phaseOffsetRegister = SI5351_REGISTER_166_CLK1_INITIAL_PHASE_OFFSET;
		clkControlRegister = SI5351_REGISTER_17_CLK1_CONTROL;
		break;
	case 2:
		baseaddr = SI5351_REGISTER_58_MULTISYNTH2_PARAMETERS_1;
		phaseOffsetRegister = SI5351_REGISTER_167_CLK2_INITIAL_PHASE_OFFSET;
		clkControlRegister = SI5351_REGISTER_18_CLK2_CONTROL;
		break;
	}

	uint8_t clkControl = 0x0C | driveStrength; // clock not inverted, powered up
	if(pllSource == SI5351_PLL_B) {
		clkControl |= (1 << 5); // Uses PLLB
	}

	if((conf->allowIntegerMode) && ((num == 0)||(div == 4))) {
		// use integer mode
		clkControl |= (1 << 6);
	}

	si5351_write(clkControlRegister, clkControl);
	si5351_writeBulk(baseaddr, P1, P2, P3, divBy4, conf->rdiv);
	si5351_write(phaseOffsetRegister, (phaseOffset & 0x7F));

	return 0;
}

// Calculates PLL, MS and RDiv settings for given Fclk in [8_000, 160_000_000] range.
// The actual frequency will differ less than 6 Hz from given Fclk, assuming `correction` is right.
void si5351_Calc(int32_t Fclk, si5351PLLConfig_t* pll_conf, si5351OutputConfig_t* out_conf)
{
	if(Fclk < 8000) Fclk = 8000;
	else if(Fclk > 160000000) Fclk = 160000000;

	out_conf->allowIntegerMode = 1;

	if(Fclk < 1000000) {
		// For frequencies in [8_000, 500_000] range we can use si5351_Calc(Fclk*64, ...) and SI5351_R_DIV_64.
		// In practice it's worth doing for any frequency below 1 MHz, since it reduces the error.
		Fclk *= 64;
		out_conf->rdiv = SI5351_R_DIV_64;
	} else {
		out_conf->rdiv = SI5351_R_DIV_1;
	}

	// Apply correction, _after_ determining rdiv.
	Fclk = Fclk - ((Fclk/1000000)*si5351Correction)/100;

	// Here we are looking for integer values of a,b,c,x,y,z such as:
	// N = a + b / c	# pll settings
	// M = x + y / z	# ms  settings
	// Fclk = Fxtal * N / M
	// N in [24, 36]
	// M in [8, 1800] or M in {4,6}
	// b < c, y < z
	// b,c,y,z <= 2**20
	// c, z != 0
	// For any Fclk in [500K, 160MHz] this algorithm finds a solution
	// such as abs(Ffound - Fclk) <= 6 Hz

	const int32_t Fxtal = 25000000;
	int32_t a, b, c, x, y, z, t;

	if(Fclk < 81000000) {
		// Valid for Fclk in 0.5..112.5 MHz range
		// However an error is > 6 Hz above 81 MHz
		a = 36; // PLL runs @ 900 MHz
		b = 0;
		c = 1;
		int32_t Fpll = 900000000;
		x = Fpll/Fclk;
		t = (Fclk >> 20) + 1;
		y = (Fpll % Fclk) / t;
		z = Fclk / t;
	} else {
		// Valid for Fclk in 75..160 MHz range
		if(Fclk >= 150000000) {
			x = 4;
		} else if (Fclk >= 100000000) {
			x = 6;
		} else {
			x = 8;
		}
		y = 0;
		z = 1;

		int32_t numerator = x*Fclk;
		a = numerator/Fxtal;
		t = (Fxtal >> 20) + 1;
		b = (numerator % Fxtal) / t;
		c = Fxtal / t;
	}

	pll_conf->mult = a;
	pll_conf->num = b;
	pll_conf->denom = c;
	out_conf->div = x;
	out_conf->num = y;
	out_conf->denom = z;
}

// Setup CLK0 for given frequency and drive strength. Use PLLA.
void si5351_SetupCLK0(int32_t Fclk, si5351DriveStrength_t driveStrength)
{
	si5351PLLConfig_t pll_conf;
	si5351OutputConfig_t out_conf;

	si5351_Calc(Fclk, &pll_conf, &out_conf);
	si5351_SetupPLL(SI5351_PLL_A, &pll_conf);
	si5351_SetupOutput(0, SI5351_PLL_A, driveStrength, &out_conf, 0);
}

// Setup CLK2 for given frequency and drive strength. Use PLLB.
void si5351_SetupCLK2(int32_t Fclk, si5351DriveStrength_t driveStrength)
{
	si5351PLLConfig_t pll_conf;
	si5351OutputConfig_t out_conf;

	si5351_Calc(Fclk, &pll_conf, &out_conf);
	si5351_SetupPLL(SI5351_PLL_B, &pll_conf);
	si5351_SetupOutput(2, SI5351_PLL_B, driveStrength, &out_conf, 0);
}

// Enables or disables outputs depending on provided bitmask.
// Examples:
// si5351_EnableOutputs(1 << 0) enables CLK0 and disables CLK1 and CLK2
// si5351_EnableOutputs((1 << 2) | (1 << 0)) enables CLK0 and CLK2 and disables CLK1
void si5351_EnableOutputs(uint8_t enabled)
{
	si5351_write(SI5351_REGISTER_3_OUTPUT_ENABLE_CONTROL, ~enabled);
}

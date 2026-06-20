#include "Devices.h"
#include <Wire.h>

// =============================================================================
//  Calibration conversion helpers
//
//  Both ADC and DAC channels share the same linear model:
//    counts = m * value + b
//    value  = (counts - b) / m
//
//  Value2Counts uses int32_t for the intermediate calculation to avoid
//  overflow on platforms where plain `int` is 16 bits (e.g. AVR), where
//  the original `counts > 65535` guard would never fire.
// =============================================================================

float Counts2Value(int counts, DACchan *dac)
{
    return (counts - dac->b) / dac->m;
}

float Counts2Value(int counts, ADCchan *adc)
{
    return (counts - adc->b) / adc->m;
}

/**
 * @brief Convert an engineering value to DAC counts, clamped to [0, 65535].
 *
 * Uses int32_t for the intermediate result so that the upper-bound clamp
 * fires correctly even on 16-bit platforms where `int` wraps at 32767.
 */
int Value2Counts(float value, DACchan *dac)
{
    int32_t counts = (int32_t)((value * dac->m) + dac->b);
    if (counts < 0)     counts = 0;
    if (counts > 65535) counts = 65535;
    return (int)counts;
}

/**
 * @brief Convert an engineering value to ADC counts, clamped to [0, 65535].
 *
 * Uses int32_t for the same reason as Value2Counts(DACchan*).
 */
int Value2Counts(float value, ADCchan *adc)
{
    int32_t counts = (int32_t)((value * adc->m) + adc->b);
    if (counts < 0)     counts = 0;
    if (counts > 65535) counts = 65535;
    return (int)counts;
}

// =============================================================================
//  Generic analog I/O wrappers
// =============================================================================

/**
 * @brief Read an ADC channel, optionally averaging @p num samples.
 *
 * When num <= 1 the function performs a single read and immediately converts
 * to engineering units.  When num > 1 it accumulates raw counts, divides by
 * num, then converts — this keeps the averaging in count space where integer
 * arithmetic is exact.
 */
float AnalogIn(int (*readadc)(int8_t chan), ADCchan *adc, int num)
{
    if (num <= 1) return Counts2Value(readadc(adc->Chan), adc);

    int32_t total = 0;
    for (int i = 0; i < num; i++) total += readadc(adc->Chan);
    return Counts2Value((int)(total / num), adc);
}

/**
 * @brief Apply a first-order IIR low-pass filter.
 *
 * Uses -1.0f as a sentinel for "not yet initialised" — the first call
 * returns newV unfiltered.  This sentinel breaks if the true signal can
 * be at or very near -1.0; callers should manage a separate init flag
 * when that is possible.
 */
float Filter(float lastV, float newV, float filter)
{
    if (lastV == -1.0f) return newV;
    return lastV * (1.0f - filter) + newV * filter;
}

/** @brief Write a calibrated engineering value to a DAC channel. */
void AnalogOut(void (*writedac)(int8_t chan, int counts), DACchan *dac, float value)
{
    writedac(dac->Chan, Value2Counts(value, dac));
}

// =============================================================================
//  AD5592 — SPI 8-channel ADC / DAC / GPIO (Analog Devices)
//
//  SPI wire format (16 bits, MSB first):
//    Bits [15:11] : register address (5 bits; bits [14:11] used as reg[3:0],
//                   bit 15 is the DAC write flag in the DAC data register)
//    Bits [11:0]  : 12-bit data payload
//
//  The caller must have already called SPI.beginTransaction() or ensured
//  the SPI bus is configured at the correct speed/mode for the AD5592
//  before invoking any of these functions.
// =============================================================================

/**
 * @brief Write a value to an AD5592 register.
 *
 * Encodes the 3-bit register address into bits [6:3] of the first byte and
 * the upper 4 bits of val into bits [3:0], then sends the low byte of val.
 */
void AD5592write(int CS, uint8_t reg, uint16_t val)
{
    digitalWrite(CS, LOW);
    SPI.transfer(((reg << 3) & 0x78) | ((val >> 8) & 0x0F));
    SPI.transfer((uint8_t)(val & 0xFF));
    digitalWrite(CS, HIGH);
}

/**
 * @brief Read a 16-bit word from the AD5592 readback path.
 *
 * Returns the raw 16-bit value shifted out during a dummy-zero transfer.
 * Changed return type to uint16_t to match the actual hardware width and
 * avoid sign-extension confusion in callers.
 */
uint16_t AD5592readWord(int CS)
{
    digitalWrite(CS, LOW);
    uint16_t val = SPI.transfer16(0);
    digitalWrite(CS, HIGH);
    return val;
}

/**
 * @brief Perform one ADC conversion on the AD5592.
 *
 * The AD5592 ADC pipeline requires a dummy read after selecting the channel
 * before the real conversion result is available.
 *
 * Result encoding: the 12-bit ADC result is returned left-justified in 16
 * bits (bits [15:4]).  Bits [3:0] are always zero.  The channel tag returned
 * by the device occupies bits [14:12] of the raw read word and is validated
 * against @p chan; a mismatch indicates a communication error.
 */
int AD5592readADC(int CS, int8_t chan)
{
    // Select the channel to convert.
    AD5592write(CS, 2, (uint16_t)(1 << chan));
    delayMicroseconds(1); // allow conversion to settle

    // Dummy read — required by the AD5592 pipeline before a real result is ready.
    digitalWrite(CS, LOW);
    SPI.transfer16(0);
    digitalWrite(CS, HIGH);

    // Read the actual conversion result.
    digitalWrite(CS, LOW);
    uint16_t val = SPI.transfer16(0);
    digitalWrite(CS, HIGH);

    // Validate the returned channel tag (bits [14:12]).
    if (((val >> 12) & 0x7) != (uint16_t)chan) return -1;

    // Left-justify the 12-bit result into bits [15:4].
    val <<= 4;
    return (int)(val & 0xFFF0);
}

/**
 * @brief Average @p num ADC conversions on one AD5592 channel.
 *
 * Each call to the single-sample overload returns a left-justified 16-bit
 * value; the sum and divide are performed in that same space, so the result
 * is also left-justified.  Returns -1 immediately on any read error.
 */
int AD5592readADC(int CS, int8_t chan, int8_t num)
{
    int32_t total = 0;
    for (int i = 0; i < num; i++)
    {
        int sample = AD5592readADC(CS, chan);
        if (sample == -1) return -1;
        total += sample;
    }
    return (int)(total / num);
}

/**
 * @brief Write a 16-bit value to one AD5592 DAC channel.
 *
 * Interrupt-safe single-slot queue: if called while a previous SPI transfer
 * is in progress (e.g. pre-empted by an ISR), the new request is stored and
 * executed automatically when the current transfer finishes.
 *
 * KNOWN LIMITATION: the queue is implemented with static locals, so it is
 * shared across all CS lines.  A second concurrent call for a *different* CS
 * line will overwrite the queued values silently.  If multiple AD5592 devices
 * need concurrent interrupt-safe writes, this function must be extended to
 * support per-device queues.
 *
 * DAC data register format (16 bits):
 *   Bit  15     : 1 (write-to-DAC flag)
 *   Bits [14:12]: channel number
 *   Bits [11:0] : 12-bit DAC value (val >> 4, discarding the lower 4 bits)
 */
void AD5592writeDAC(int CS, int8_t chan, int val)
{
    // Static locals implement the single-slot interrupt-safe queue.
    static bool   busy   = false;
    static bool   queued = false; // BUG FIX: was "queded" (typo)
    static int    qCS;
    static int    qval;
    static int8_t qchan;

    if (busy)
    {
        // Pre-empted — save parameters and return; the ongoing transfer will
        // drain the queue when it finishes.
        queued = true;
        qCS    = CS;
        qval   = val;
        qchan  = chan;
        return;
    }

    busy = true;

    // Build the 16-bit DAC data register word.
    uint16_t d = (uint16_t)(((val >> 4) & 0x0FFF)
                            | (((uint16_t)chan) << 12)
                            | 0x8000);

    // Perform the SPI transfer.  Use a local discard variable rather than
    // reusing the `val` parameter — reusing `val` was legal but confusing.
    digitalWrite(CS, LOW);
    (void)SPI.transfer((uint8_t)(d >> 8));
    (void)SPI.transfer((uint8_t)(d & 0xFF));
    digitalWrite(CS, HIGH);

    busy = false;

    // If a request arrived while we were busy, execute it now.
    if (queued)
    {
        queued = false;
        AD5592writeDAC(qCS, qchan, qval);
    }
}

// =============================================================================
//  AD5593R — I²C 8-channel ADC / DAC / GPIO (Analog Devices)
//
//  The AD5593R is a functionally identical variant of the AD5592 with an I²C
//  interface instead of SPI.  Register layout and calibration are the same;
//  only the transport layer differs.
//
//  All functions use the Arduino Wire library.  The caller is responsible for
//  calling Wire.begin() and setting the bus speed before using these routines.
// =============================================================================

/**
 * @brief Write a 16-bit value to an AD5593R register over I²C.
 *
 * Transmits: [addr | W] [pb] [val_high] [val_low]
 *
 * @return 0 on success; Wire error code on failure (see Wire.endTransmission()).
 */
int AD5593write(uint8_t addr, uint8_t pb, uint16_t val)
{
    Wire.beginTransmission(addr);
    Wire.write(pb);
    Wire.write((uint8_t)((val >> 8) & 0xFF));
    Wire.write((uint8_t)(val & 0xFF));
    return Wire.endTransmission();
}

/**
 * @brief Read a 16-bit word from an AD5593R register over I²C.
 *
 * Sends the pointer-byte, then reads two bytes MSB-first.
 *
 * @return 16-bit value, or -1 on any I²C error.
 */
int AD5593readWord(uint8_t addr, uint8_t pb)
{
    Wire.beginTransmission(addr);
    Wire.write(pb);
    int iStat = Wire.endTransmission();
    if (iStat != 0) return -1;

    Wire.requestFrom(addr, (uint8_t)2);
    int result = (Wire.read() << 8) | Wire.read();
    return result;
}

/**
 * @brief Perform a single ADC conversion on one AD5593R channel.
 *
 * Selects the channel via register 0x02, then reads the result from register
 * 0x40.  The returned channel tag in bits [14:12] is validated against @p chan.
 *
 * @return Left-justified 12-bit result (bits [15:4]), or -1 on error.
 *
 * Note on the mask: the original `i & 0xFFFF` was redundant on 32-bit platforms
 * and could silently truncate on 16-bit ones.  Replaced with a cast to uint16_t
 * applied only to the valid result bits, then returned as int for consistency
 * with the error-return convention.
 */
int AD5593readADC(int8_t addr, int8_t chan)
{
    // Select the ADC channel.
    if (AD5593write((uint8_t)addr, 0x02, (uint16_t)(1 << chan)) != 0) return -1;

    // Read the conversion result from the ADC readback register.
    int raw = AD5593readWord((uint8_t)addr, 0x40);
    if (raw < 0) return -1;

    // Validate the returned channel tag (bits [14:12]).
    if (((raw >> 12) & 0x7) != chan) return -1;

    // Left-justify the 12-bit result into bits [15:4] and return.
    uint16_t result = (uint16_t)((raw << 4) & 0xFFF0);
    return (int)result;
}

/**
 * @brief Average @p num ADC conversions on one AD5593R channel.
 *
 * Returns -1 immediately on any individual read error.
 */
int AD5593readADC(int8_t addr, int8_t chan, int8_t num)
{
    int32_t total = 0;
    for (int i = 0; i < num; i++)
    {
        int sample = AD5593readADC(addr, chan);
        if (sample == -1) return -1;
        total += sample;
    }
    return (int)(total / num);
}

/**
 * @brief Write a 16-bit value to one AD5593R DAC channel over I²C.
 *
 * DAC data register format for the AD5593R:
 *   Bit  15     : 1 (DAC write flag)
 *   Bits [14:12]: channel number (also encoded in the pointer-byte as 0x10|chan)
 *   Bits [11:0] : 12-bit DAC value (val >> 4)
 *
 * Note: the channel bits appear in both the pointer-byte (`0x10 | chan`) and
 * in the data word, which matches the AD5593R datasheet register map (section
 * 8.3.2).
 *
 * @return 0 on success; non-zero I²C error code on failure.
 */
int AD5593writeDAC(int8_t addr, int8_t chan, int val)
{
    // Build the 16-bit data word: DAC flag | channel | 12-bit value.
    uint16_t d = (uint16_t)(((val >> 4) & 0x0FFF)
                            | ((uint16_t)chan << 12)
                            | 0x8000);
    return AD5593write((uint8_t)addr, (uint8_t)(0x10 | chan), d);
}

// =============================================================================
//  DAC8571 — I²C 16-bit single-channel DAC (Texas Instruments)
// =============================================================================

/**
 * @brief Write a value to the DAC8571 over I²C.
 *
 * The DAC8571 accepts a 16-bit value via a 3-byte I²C transaction:
 *   [addr | W] [0x10 (fast write command)] [MSB] [LSB]
 *
 * The supplied @p val is scaled by 2 before transmission; this maps the
 * caller's [0, 32767] input range to the device's [0, 65534] output range.
 *
 * If @p addr is 0 the function returns 0 without performing any I²C
 * transaction.  This is retained for backward compatibility; callers that
 * need to distinguish "address not set" from "write succeeded" should
 * validate the address themselves before calling.
 */
int DAC8571(int8_t addr, int val)
{
    if (addr == 0) return 0; // address not configured — no-op

    int scaled = val * 2; // scale to the device's 16-bit range

    Wire.beginTransmission((uint8_t)addr);
    Wire.write(0x10);                              // fast write command byte
    Wire.write((uint8_t)((scaled >> 8) & 0xFF));  // MSB
    Wire.write((uint8_t)(scaled & 0xFF));          // LSB
    return Wire.endTransmission();
}

// =============================================================================
//  MCP4725 — I²C 12-bit single-channel DAC (Microchip)
// =============================================================================

/**
 * @brief Write a 12-bit value to an MCP4725 DAC over I²C.
 *
 * I²C transaction: [addr | W] [cmd] [value_high_byte] [value_lower_nibble]
 *
 * The @p value argument must be pre-shifted left by 4 bits by the caller
 * so that the 12-bit data occupies bits [15:4].  The device ignores the
 * lower nibble of the third byte.
 *
 * Command codes:
 *   0x40 — write DAC register only (volatile; lost on power-off)
 *   0x60 — write DAC register and EEPROM (persists across power-off)
 */
void MCP4725(uint8_t addr, uint8_t cmd, uint16_t value)
{
    Wire.beginTransmission(addr);
    Wire.write(cmd);
    Wire.write((uint8_t)(value >> 8));       // upper byte: bits [15:8] (data [11:4])
    Wire.write((uint8_t)(value & 0xF0));     // lower byte: bits [7:4] (data [3:0]), lower nibble ignored
    Wire.endTransmission();
}

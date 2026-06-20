#ifndef DEVICES_H
#define DEVICES_H

/**
 * @file Devices.h
 * @brief Low-level driver interfaces for analog/digital I/O devices.
 *
 * Provides:
 *  - Linear calibration helpers (Counts2Value / Value2Counts) for ADC and DAC
 *    channels, using the slope/intercept model:
 *      counts = m * value + b
 *      value  = (counts - b) / m
 *  - AnalogIn / AnalogOut wrappers that apply calibration and optional averaging.
 *  - A first-order IIR low-pass filter helper (Filter).
 *  - Low-level SPI drivers for the Analog Devices AD5592 8-channel ADC/DAC/GPIO.
 *  - Low-level I²C drivers for the Analog Devices AD5593R (I²C variant of AD5592).
 *  - Low-level I²C driver for the Texas Instruments DAC8571 16-bit DAC.
 *  - Low-level I²C driver for the Microchip MCP4725 12-bit DAC.
 *
 * Callers are responsible for initialising the SPI/Wire buses and configuring
 * chip-select / address pins before calling these functions.
 *
 * Note on AD5592 vs AD5593R naming:
 *   The SPI device is referred to as "AD5592" throughout; the I²C variant is
 *   referred to as "AD5593" (the datasheet name is AD5593R).
 */

#include <Arduino.h>   // Corrected case (Linux FS is case-sensitive)
#include <SPI.h>

// ---------------------------------------------------------------------------
// Device enumeration (used by higher-level module code to tag channel types)
// ---------------------------------------------------------------------------

enum Devices
{
    devADC,    ///< Generic ADC channel
    devPWM,    ///< PWM output channel
    devAD5592  ///< AD5592 SPI ADC/DAC/GPIO channel
};

// ---------------------------------------------------------------------------
// Calibration channel descriptors
// ---------------------------------------------------------------------------

/**
 * @brief Calibration parameters for one ADC input channel.
 *
 * The linear model applied is:
 *   engineering_value = (raw_counts - b) / m
 *   raw_counts        = m * engineering_value + b
 */
typedef struct
{
    int8_t Chan;  ///< Hardware channel index (0 … max channels for the chip)
    float  m;     ///< Slope:     counts per engineering unit
    float  b;     ///< Intercept: counts at engineering value zero
} ADCchan;

/**
 * @brief Calibration parameters for one DAC output channel.
 *
 * The linear model applied is:
 *   dac_counts        = m * engineering_value + b
 *   engineering_value = (dac_counts - b) / m
 */
typedef struct
{
    int8_t Chan;  ///< Hardware channel index (0 … max channels for the chip)
    float  m;     ///< Slope:     counts per engineering unit
    float  b;     ///< Intercept: counts at engineering value zero
} DACchan;

// ---------------------------------------------------------------------------
// Generic calibration conversion helpers
// ---------------------------------------------------------------------------

/**
 * @brief Convert raw ADC counts to an engineering value using stored calibration.
 * @param counts  Raw integer count read from hardware.
 * @param adc     Calibration parameters for this channel.
 * @return        Engineering value (e.g. volts, amps …).
 */
float Counts2Value(int counts, ADCchan *adc);

/**
 * @brief Convert raw DAC counts to an engineering value using stored calibration.
 * @param counts  Raw integer count read back from hardware (e.g. for verification).
 * @param dac     Calibration parameters for this channel.
 * @return        Engineering value.
 */
float Counts2Value(int counts, DACchan *dac);

/**
 * @brief Convert an engineering value to DAC counts, clamped to [0, 65535].
 * @param value  Desired engineering value.
 * @param dac    Calibration parameters for this channel.
 * @return       Integer DAC counts, saturated at 0 and 65535.
 */
int Value2Counts(float value, DACchan *dac);

/**
 * @brief Convert an engineering value to ADC counts, clamped to [0, 65535].
 *
 * Useful when computing a setpoint in ADC count space from an engineering target.
 * @param value  Desired engineering value.
 * @param adc    Calibration parameters for this channel.
 * @return       Integer counts, saturated at 0 and 65535.
 */
int Value2Counts(float value, ADCchan *adc);

// ---------------------------------------------------------------------------
// Generic analog I/O wrappers
// ---------------------------------------------------------------------------

/**
 * @brief Read an ADC channel, optionally averaging multiple samples.
 *
 * @param readadc  Function pointer to the hardware read routine.
 *                 Signature: int fn(int8_t channel) — returns raw counts or -1
 *                 on error.
 * @param adc      Calibration parameters for this channel.
 * @param num      Number of samples to average (default 1).
 *                 If num <= 1, a single unconverted read is returned.
 * @return         Calibrated engineering value.
 */
float AnalogIn(int (*readadc)(int8_t chan), ADCchan *adc, int num = 1);

/**
 * @brief Write a calibrated engineering value to a DAC channel.
 *
 * Converts the engineering value to counts via Value2Counts(), then calls
 * the supplied hardware write function.
 *
 * @param writedac  Function pointer to the hardware write routine.
 *                  Signature: void fn(int8_t channel, int counts).
 * @param dac       Calibration parameters for this channel.
 * @param value     Engineering value to output.
 */
void AnalogOut(void (*writedac)(int8_t chan, int counts), DACchan *dac, float value);

/**
 * @brief First-order IIR (exponential moving average) low-pass filter.
 *
 * Formula:  output = lastV * (1 - filter) + newV * filter
 *
 * @param lastV   Previous filtered output.  Pass -1.0f on the first call to
 *                bypass filtering and return newV directly.
 *                WARNING: the -1.0f sentinel breaks down if the true signal
 *                can legitimately be at or near -1.  Callers should manage a
 *                separate "initialised" flag if that is possible.
 * @param newV    Latest raw measurement.
 * @param filter  Smoothing factor in (0, 1].  Smaller values give more
 *                smoothing (slower response); 1.0 gives no smoothing.
 *                Default is 0.1.
 * @return        Filtered output value.
 */
float Filter(float lastV, float newV, float filter = 0.1f);

// ---------------------------------------------------------------------------
// AD5592 — SPI 8-channel ADC / DAC / GPIO (Analog Devices)
// ---------------------------------------------------------------------------

/**
 * @brief Write a 16-bit value to an AD5592 register over SPI.
 *
 * The 3-bit register address is encoded in the upper nibble of the first byte;
 * the 12-bit data payload follows in the remaining bits.
 *
 * @param CS   Arduino pin number of the active-low chip-select line.
 * @param reg  AD5592 register address (3 bits, 0–7).
 * @param val  12-bit data value to write.
 */
void AD5592write(int CS, uint8_t reg, uint16_t val);

/**
 * @brief Read a 16-bit word from the AD5592 readback register over SPI.
 *
 * Performs a single 16-bit SPI transfer with CS asserted.  The caller is
 * responsible for setting up the correct readback mode beforehand.
 *
 * @param CS  Arduino pin number of the chip-select line.
 * @return    Raw 16-bit word read from the device.
 */
uint16_t AD5592readWord(int CS);

/**
 * @brief Perform a single ADC conversion on one AD5592 channel.
 *
 * Two SPI transactions are always issued: a dummy read (required by the
 * AD5592 pipeline) followed by the actual result read.
 *
 * The returned value is left-justified in 16 bits (12-bit result in bits
 * [15:4], bits [3:0] always zero).
 *
 * @param CS    Chip-select pin.
 * @param chan  ADC channel to convert (0–7).
 * @return      Left-justified 12-bit result, or -1 if the returned channel
 *              tag does not match @p chan (communication error).
 */
int AD5592readADC(int CS, int8_t chan);

/**
 * @brief Average multiple ADC conversions on one AD5592 channel.
 *
 * Calls the single-sample overload @p num times and returns the integer mean.
 * Returns -1 immediately if any individual read returns -1.
 *
 * @param CS    Chip-select pin.
 * @param chan  ADC channel (0–7).
 * @param num   Number of samples to average.
 * @return      Averaged left-justified result, or -1 on any read error.
 */
int AD5592readADC(int CS, int8_t chan, int8_t num);

/**
 * @brief Write a 16-bit value to one AD5592 DAC channel over SPI.
 *
 * This function is interrupt-safe in a single-producer sense: if a call
 * arrives while a previous transfer is in progress (e.g. from an ISR), the
 * new request is queued and automatically executed when the current transfer
 * completes.  Only one request can be queued; a second mid-transfer call
 * overwrites the queue silently.
 *
 * LIMITATION: the queue is implemented with module-level statics, so it is
 * shared across all CS lines.  Two concurrent calls for *different* CS lines
 * will still race.
 *
 * @param CS    Chip-select pin.
 * @param chan  DAC channel (0–7).
 * @param val   16-bit DAC value (full scale = 65535).
 */
void AD5592writeDAC(int CS, int8_t chan, int val);

// ---------------------------------------------------------------------------
// AD5593R — I²C 8-channel ADC / DAC / GPIO (Analog Devices)
// ---------------------------------------------------------------------------

/**
 * @brief Write a 16-bit value to an AD5593R register over I²C.
 *
 * @param addr  7-bit I²C device address.
 * @param pb    Pointer-byte (register address / command byte).
 * @param val   16-bit value to write (MSB first).
 * @return      0 on success; non-zero Wire error code on failure.
 */
int AD5593write(uint8_t addr, uint8_t pb, uint16_t val);

/**
 * @brief Read a 16-bit word from an AD5593R register over I²C.
 *
 * Writes the pointer-byte, then reads two bytes (MSB first).
 *
 * @param addr  7-bit I²C device address.
 * @param pb    Pointer-byte (register address).
 * @return      16-bit value read, or -1 on any I²C error.
 */
int AD5593readWord(uint8_t addr, uint8_t pb);

/**
 * @brief Perform a single ADC conversion on one AD5593R channel.
 *
 * @param addr  7-bit I²C device address.
 * @param chan  ADC channel (0–7).
 * @return      Left-justified 12-bit result (bits [15:4]), or -1 on error.
 */
int AD5593readADC(int8_t addr, int8_t chan);

/**
 * @brief Average multiple ADC conversions on one AD5593R channel.
 *
 * @param addr  7-bit I²C device address.
 * @param chan  ADC channel (0–7).
 * @param num   Number of samples to average.
 * @return      Averaged result, or -1 on any read error.
 */
int AD5593readADC(int8_t addr, int8_t chan, int8_t num);

/**
 * @brief Write a 16-bit value to one AD5593R DAC channel over I²C.
 *
 * @param addr  7-bit I²C device address.
 * @param chan  DAC channel (0–7).
 * @param val   16-bit DAC value (full scale = 65535).
 * @return      0 on success; non-zero I²C error code on failure.
 */
int AD5593writeDAC(int8_t addr, int8_t chan, int val);

// ---------------------------------------------------------------------------
// DAC8571 — I²C 16-bit single-channel DAC (Texas Instruments)
// ---------------------------------------------------------------------------

/**
 * @brief Write a 16-bit value to a DAC8571 over I²C.
 *
 * The DAC8571 expects a 16-bit value; internally the function scales the
 * supplied @p val by 2 to map the full-scale range.
 *
 * @param addr  7-bit I²C device address.  If 0, the function returns
 *              ERR_INVALID_ADDR without performing any I²C transaction.
 *              NOTE: this returns 0 (normally "success") for backward
 *              compatibility — callers that need to distinguish this case
 *              should validate @p addr before calling.
 * @param val   Value to write (0–32767 before the ×2 scaling).
 * @return      0 on success; non-zero Wire error code on I²C failure;
 *              0 also if addr == 0 (see note above).
 */
int DAC8571(int8_t addr, int val);

// ---------------------------------------------------------------------------
// MCP4725 — I²C 12-bit single-channel DAC (Microchip)
// ---------------------------------------------------------------------------

/**
 * @brief Write a 12-bit value to an MCP4725 DAC over I²C.
 *
 * @param addr   7-bit I²C device address (default factory address is 0x62).
 * @param cmd    Command byte:
 *                 0x40 — write to DAC output register only (volatile)
 *                 0x60 — write to DAC register and EEPROM (persists across power-off)
 * @param value  12-bit DAC value, left-shifted by 4 bits into bits [15:4].
 *               The lower nibble is ignored by the device.
 */
void MCP4725(uint8_t addr, uint8_t cmd, uint16_t value);

#endif // DEVICES_H

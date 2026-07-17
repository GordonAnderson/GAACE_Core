#pragma once
//
// GHal.h  -  optional hardware-abstraction hook for GAACE_Core's numbered
// pin / analog debug commands on pure STM32Cube.
//
// GAACE_Core's `debug` module has two tiers of commands:
//   * PORTABLE CORE (always available): memory access, uptime, reset, uuid,
//     cputemp, ram. These need no pin numbering.
//   * PIN / ANALOG commands (PINMODE/DOUT/DIN/ADC/DAC/ADCRES/DACRES): these
//     address GPIO/ADC by an INTEGER, which is natural on Arduino but has no
//     built-in meaning on STM32 (pins are port+pin pairs, ADC is channels on
//     specific instances).
//
// To keep GAACE_Core board-agnostic, the pin/analog commands call through this
// small interface, which the PROJECT implements against its own pin map / ADC /
// DAC. If no GHal is registered, the pin/analog debug commands return NAK ("not
// supported on this build") and the portable core still works.
//
// On Arduino builds this interface is unused (debug calls pinMode/analogRead
// directly), so GHal is only compiled on the pure-Cube path.

#include "gaace_compat.h"

#if !defined(ARDUINO)

#include <stdint.h>

// Abstract hook. A project implements these against its own pin map / ADC / DAC.
class GHal
{
public:
    virtual ~GHal() = default;

    // Digital ----------------------------------------------------------------
    // mode: 0 = input, 1 = output, 2 = input-pullup
    virtual bool pinMode(int pin, int mode)      = 0;
    virtual bool digitalWrite(int pin, int high) = 0;
    virtual int  digitalRead(int pin)            = 0;   // 0/1, or -1 if invalid

    // Analog -----------------------------------------------------------------
    virtual bool setADCResolutionBits(int bits)  { (void)bits; return false; }
    virtual bool setDACResolutionBits(int bits)  { (void)bits; return false; }
    virtual int  analogRead(int channel)         { (void)channel; return -1; }
    virtual bool analogWrite(int channel, int v) { (void)channel; (void)v; return false; }

    // Internal temperature sensor -------------------------------------------
    // Return the raw ADC count of the MCU's internal temperature channel
    // (project configures which ADC / VREF), or -1 if not wired up. The debug
    // module applies the H7 factory-calibration formula to convert to degC.
    virtual int  readInternalTempRaw()           { return -1; }
};

// The project registers its GHal once at startup:  debugSetHal(&myHal);
// Declared here, defined in debug.cpp. NULL by default -> pin/analog cmds NAK.
void  debugSetHal(GHal *hal);
GHal *debugGetHal(void);

#endif // !ARDUINO

#pragma once
//
// gaace_compat.h  -  tiny compatibility layer that lets GAACE_Core build on
// BOTH pure STM32Cube (no Arduino) AND Arduino platforms from the same source.
//
// It provides the small Arduino spellings GAACE_Core uses (`byte`, `constrain`)
// and pulls in GStream (GAACE_Core's own Stream/Print). On Arduino builds it
// includes <Arduino.h> and does NOT redefine anything the framework already
// provides. Include this instead of <Arduino.h> at the top of GAACE_Core
// sources.

#include <stdint.h>
#include <stddef.h>   // ptrdiff_t
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(ARDUINO)
  // Arduino build: the framework already defines byte, constrain, Stream, etc.
  // Pull it in and let GStream.h below add only the GStream base class (which
  // GArduinoStream bridges to the framework's Stream).
  #include <Arduino.h>
#endif

#include "GStream.h"     // GAACE_Core's Stream/Print base + DEC/HEX constants

#if !defined(ARDUINO)
  // Pure-Cube build: provide the Arduino spellings GAACE_Core relies on.
  #ifndef GAACE_BYTE_DEFINED
  #define GAACE_BYTE_DEFINED
  typedef uint8_t byte;
  #endif

  #ifndef constrain
  #define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
  #endif
#endif

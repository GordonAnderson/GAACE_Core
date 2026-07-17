#pragma once
//
// GArduinoStream.h  -  bridges an Arduino framework Stream (Serial, SerialUSB,
// Serial1, a SoftwareSerial, etc.) to GAACE_Core's GStream base.
//
// ONE adapter covers ALL Arduino-framework platforms (SAMD21/SAMD51/SAM3X8/
// ESP32/Teensy, ...), because they all expose the same Arduino Stream API.
// This is the low-friction path for validating the arduino-free GAACE_Core on
// existing hardware: point an existing project at the branch, wrap its Serial
// in a GArduinoStream, and register that with the commandProcessor.
//
// This header only compiles on Arduino builds (it needs the framework's
// Stream). It is #ifdef-guarded so it is harmless to include on Cube builds.
//
// DESIGN NOTE: this adapter forwards ONLY the three primitives
// (available / read / write) to the underlying Arduino Stream. It does NOT
// forward print()/println() -- those are provided by GStream's own formatting,
// so numeric/string output is byte-for-byte identical across every platform
// (Arduino and Cube alike). That uniformity matters for a host that parses the
// command protocol's output.
//
// USAGE (on an Arduino project pointed at the arduino-free branch):
//
//   #include "GArduinoStream.h"
//   GArduinoStream usbStream(SerialUSB);   // or Serial, Serial1, ...
//   ...
//   cp.registerStream(&usbStream);
//
//   void setup() { SerialUSB.begin(115200); ... }

#include "GStream.h"

#if defined(ARDUINO)

#include <Arduino.h>   // for the framework's Stream class

class GArduinoStream : public GStream {
public:
  // Wraps any Arduino Stream (Serial, SerialUSB, Serial1, SoftwareSerial, ...).
  explicit GArduinoStream(Stream &s) : _s(s) {}

  int available() override { return _s.available(); }
  int read()      override { return _s.read(); }

  size_t write(uint8_t b) override { return _s.write(b); }

  // Forward the bulk path too so it uses the framework's (often more efficient)
  // block write rather than GStream's default byte loop.
  size_t write(const uint8_t *buf, size_t len) override {
    return _s.write(buf, len);
  }

private:
  Stream &_s;   // the framework's Stream (NOT GStream) -- this is the bridge
};

#endif // ARDUINO

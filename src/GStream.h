#pragma once
//
// GStream.h  -  minimal Stream/Print shim for GAACE_Core on pure STM32Cube
//
// GAACE_Core's command processor (and Button/debug/Devices to a lesser extent)
// were written against the Arduino Stream/Print classes. On a native STM32Cube
// (HAL/LL) project those classes do not exist. This header provides a tiny,
// dependency-free equivalent that exposes ONLY the surface GAACE actually uses:
//
//     available()  read()  write()  print(...)  println(...)
//
// plus the DEC/HEX format constants. Concrete transports (HAL UART, USB CDC)
// subclass GStream and implement the three lowest-level virtuals. Everything
// else (the print/println formatting) is provided here once, so every
// transport gets consistent numeric formatting for free.
//
// This is intentionally NOT a general-purpose Arduino Print reimplementation --
// it is exactly what GAACE_Core needs and no more, which keeps it auditable.
//
// Naming: the class is GStream (not Stream) to avoid any clash if an Arduino
// core header is ever transitively included. A `using Stream = GStream;` alias
// at the bottom lets GAACE_Core source compile unmodified where it says Stream*.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Arduino-compatible radix constants (values match Arduino so existing table
// entries using DEC/HEX keep working).
#ifndef DEC
#define DEC 10
#endif
#ifndef HEX
#define HEX 16
#endif
#ifndef OCT
#define OCT 8
#endif
#ifndef BIN
#define BIN 2
#endif

class GStream {
public:
  virtual ~GStream() = default;

  // ----- lowest-level transport interface: implement these in subclasses -----
  virtual int  available() = 0;             // bytes available to read
  virtual int  read()      = 0;             // next byte, or -1 if none
  virtual size_t write(uint8_t b) = 0;      // send one byte; return count sent

  // Optional bulk write; default loops over write(byte). Transports with a
  // native block write (USB CDC, DMA UART) should override for efficiency.
  virtual size_t write(const uint8_t *buf, size_t len) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++) n += write(buf[i]);
    return n;
  }

  // ----- print() surface used by GAACE_Core -----
  size_t print(const char *s) {
    if (!s) return 0;
    return write(reinterpret_cast<const uint8_t *>(s), strlen(s));
  }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }

  size_t print(int v, int fmt = DEC) {
    char b[16];
    fmtInt(b, sizeof(b), (long)v, fmt, /*isSigned*/ true);
    return print(b);
  }
  size_t print(unsigned int v, int fmt = DEC) {
    char b[16];
    fmtInt(b, sizeof(b), (unsigned long)v, fmt, false);
    return print(b);
  }
  size_t print(long v, int fmt = DEC) {
    char b[24];
    fmtInt(b, sizeof(b), v, fmt, true);
    return print(b);
  }
  size_t print(unsigned long v, int fmt = DEC) {
    char b[24];
    fmtInt(b, sizeof(b), v, fmt, false);
    return print(b);
  }
  size_t print(uint8_t v, int fmt = DEC) { return print((unsigned int)v, fmt); }

  size_t print(double v, int digits = 2) {
    char b[32];
    // %.*f gives fixed-point with `digits` decimals, matching Arduino default.
    snprintf(b, sizeof(b), "%.*f", digits, v);
    return print(b);
  }
  // float folds to the double path (same as Arduino).
  size_t print(float v, int digits = 2) { return print((double)v, digits); }

  // ----- println() surface -----
  size_t println()                 { return print("\r\n"); }
  size_t println(const char *s)    { size_t n = print(s); return n + println(); }
  size_t println(char c)           { size_t n = print(c); return n + println(); }
  size_t println(int v, int f=DEC) { size_t n = print(v,f); return n + println(); }
  size_t println(unsigned int v, int f=DEC){ size_t n=print(v,f); return n+println(); }
  size_t println(long v, int f=DEC){ size_t n=print(v,f); return n+println(); }
  size_t println(unsigned long v,int f=DEC){ size_t n=print(v,f); return n+println(); }
  size_t println(uint8_t v,int f=DEC){ size_t n=print(v,f); return n+println(); }
  size_t println(double v,int d=2) { size_t n=print(v,d); return n+println(); }
  size_t println(float v,int d=2)  { size_t n=print(v,d); return n+println(); }

private:
  // Integer formatting for arbitrary radix (matches Arduino DEC/HEX output:
  // hex is lowercase, no "0x" prefix, which is what GAACE expects).
  static void fmtInt(char *out, size_t cap, long value, int base, bool isSigned) {
    if (base < 2)  base = 10;
    if (base > 16) base = 16;
    // Fast path for decimal via snprintf (handles sign cleanly).
    if (base == 10) {
      if (isSigned) snprintf(out, cap, "%ld", value);
      else          snprintf(out, cap, "%lu", (unsigned long)value);
      return;
    }
    // Arbitrary base (hex/oct/bin): work on the unsigned magnitude. Arduino
    // treats hex/oct/bin of a signed value as its unsigned bit pattern.
    unsigned long v = (unsigned long)value;
    char tmp[40];
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    const char *digits = "0123456789abcdef";
    while (v > 0 && i < (int)sizeof(tmp)) { tmp[i++] = digits[v % base]; v /= base; }
    int o = 0;
    while (i > 0 && o < (int)cap - 1) out[o++] = tmp[--i];
    out[o] = '\0';
  }

  // Unsigned-long overload for fmtInt (used by the unsigned print paths).
  static void fmtInt(char *out, size_t cap, unsigned long value, int base, bool /*isSigned*/) {
    fmtInt(out, cap, (long)value, base, false);
  }
};

// NOTE: GAACE_Core source refers to GStream directly (not `Stream`), so no
// alias is needed and there is no clash with Arduino's own `Stream` class.
// Arduino projects bridge the framework's Stream to this class via
// GArduinoStream (see GArduinoStream.h), which IS-A GStream.

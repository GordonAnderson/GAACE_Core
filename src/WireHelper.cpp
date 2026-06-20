#include "WireHelper.h"

// =============================================================================
//  Construction / configuration
// =============================================================================

WireHelper::WireHelper()
    : wire(&Wire)
    , returnAvailable(false)
{
    // sb (SerialBuffer) is default-constructed.
    // wire defaults to the global Wire instance; call SetInterface() to change.
}

void WireHelper::SetInterface(TwoWire *w)
{
    wire = w;
}

// =============================================================================
//  Read helpers
//
//  All functions check wire->available() before each byte read.  No blocking
//  occurs; the caller receives false / -1 immediately on underflow.
//  All multi-byte values are assembled little-endian (LSB first).
//
//  Implementation note on avoiding shift UB:
//  -----------------------------------------
//  The original ReadUnsignedWord() and ReadInt() used:
//    i |= wire->read() << 8;    // and << 16, << 24
//  On 16-bit AVR, `int` is 16 bits.  Shifting into or past the sign bit of a
//  signed type is undefined behaviour in C++.  The fixed versions use either
//  uint8_t byte-pointer casting (ReadInt, Read16bitInt, ReadFloat) or
//  explicit uint16_t / uint32_t intermediate types (ReadUnsignedWord) so that
//  all shifts operate on unsigned types wide enough to hold the result.
// =============================================================================

/**
 * @brief Read one unsigned byte from the Wire receive buffer.
 *
 * BUG FIX: original declared `int i` unnecessarily.  Simplified to a direct
 * read with a mask, returned inline.
 */
int WireHelper::ReadUnsignedByte(void)
{
    if (wire->available() == 0) return -1;
    return (int)(wire->read() & 0xFF);
}

/** @brief Read one signed byte. */
bool WireHelper::ReadByte(int8_t *b)
{
    if (wire->available() == 0) return false;
    *b = (int8_t)wire->read();
    return true;
}

/** @brief Read one boolean byte. */
bool WireHelper::ReadBool(bool *b)
{
    if (wire->available() == 0) return false;
    *b = (wire->read() != 0);
    return true;
}

/**
 * @brief Read a little-endian unsigned 16-bit word.
 *
 * BUG FIX: original assembled the value into a plain `int` using
 *   i |= wire->read() << 8;
 * On 16-bit AVR (int == 16 bits) this shifts into the sign bit — UB.
 * Fixed by using a uint16_t accumulator so the shift is well-defined.
 *
 * Returns -1 (as int) on underflow, value in [0, 65535] on success.
 */
int WireHelper::ReadUnsignedWord(void)
{
    if (wire->available() == 0) return -1;
    uint16_t lo = (uint8_t)wire->read();
    if (wire->available() == 0) return -1;
    uint16_t hi = (uint8_t)wire->read();
    return (int)((hi << 8) | lo);
}

/**
 * @brief Read a little-endian signed 16-bit integer.
 *
 * Uses byte-pointer casting to fill the int16_t directly, which is
 * well-defined for any platform endianness and avoids shift UB.
 */
bool WireHelper::Read16bitInt(int16_t *val)
{
    uint8_t *b = (uint8_t *)val;
    if (wire->available() == 0) return false;
    b[0] = (uint8_t)wire->read(); // LSB
    if (wire->available() == 0) return false;
    b[1] = (uint8_t)wire->read(); // MSB
    return true;
}

/**
 * @brief Read a little-endian signed 32-bit integer.
 *
 * BUG FIX: original parameter was `int *i`, and assembled with:
 *   *i |= wire->read() << 16;  // and << 24
 * On AVR (int == 16 bits) these shifts are undefined behaviour.
 * Parameter changed to int32_t* and implementation uses byte-pointer
 * casting, which is portable and well-defined on all platforms.
 */
bool WireHelper::ReadInt(int32_t *val)
{
    uint8_t *b = (uint8_t *)val;
    for (int j = 0; j < 4; j++)
    {
        if (wire->available() == 0) return false;
        b[j] = (uint8_t)wire->read(); // fills LSB..MSB in order
    }
    return true;
}

/**
 * @brief Read a 4-byte IEEE 754 float (little-endian byte order).
 *
 * BUG FIX: original declared `int i` that was never used (compiler warning).
 * Removed.  Implementation is otherwise identical — byte-pointer casting into
 * the float is the correct portable approach.
 */
bool WireHelper::ReadFloat(float *fval)
{
    uint8_t *b = (uint8_t *)fval;
    for (int j = 0; j < 4; j++)
    {
        if (wire->available() == 0) return false;
        b[j] = (uint8_t)wire->read();
    }
    return true;
}

// =============================================================================
//  Send helpers
//
//  These functions write typed values into the internal SerialBuffer (sb).
//  Bytes sit there until requestEventProcessor() is called from the I²C
//  requestEvent handler.
//  All values are staged little-endian (LSB first).
// =============================================================================

/** @brief Stage one boolean byte. */
void WireHelper::SendBool(bool bval)
{
    sb.write((uint8_t)bval);
}

/** @brief Stage one unsigned byte. */
void WireHelper::SendByte(uint8_t bval)
{
    sb.write(bval);
}

/**
 * @brief Stage a little-endian 16-bit signed integer (2 bytes).
 *
 * BUG FIX: original took `int ival`, which is 32 bits on ARM/SAMD.  The
 * upper two bytes were silently dropped.  Parameter changed to int16_t to
 * make the 2-byte width contract visible at the call site.
 */
void WireHelper::SendWord(int16_t ival)
{
    const uint8_t *b = (const uint8_t *)&ival;
    sb.write(b[0]); // LSB
    sb.write(b[1]); // MSB
}

/**
 * @brief Stage the low 3 bytes of @p ival as a 24-bit little-endian value.
 *
 * Only bytes [0], [1], [2] of the int32_t are sent; byte [3] (MSB) is always
 * dropped.  The caller must ensure the value fits in 24 bits.
 * Parameter changed from `int` to `int32_t` for portability.
 */
void WireHelper::SendInt24(int32_t ival)
{
    const uint8_t *b = (const uint8_t *)&ival;
    sb.write(b[0]); // bits  7: 0
    sb.write(b[1]); // bits 15: 8
    sb.write(b[2]); // bits 23:16  (b[3] intentionally omitted)
}

/** @brief Stage a 4-byte IEEE 754 float (little-endian). */
void WireHelper::SendFloat(float fval)
{
    const uint8_t *b = (const uint8_t *)&fval;
    sb.write(b[0]);
    sb.write(b[1]);
    sb.write(b[2]);
    sb.write(b[3]);
}

// =============================================================================
//  Request-event handler
// =============================================================================

/**
 * @brief Flush staged bytes (or available-byte count) to the I²C master.
 *
 * Call from Wire.onRequest() handler only.
 *
 * Two modes:
 *  - Normal mode: sends up to WIRE_MAX_SEND_BYTES bytes from sb.
 *  - Available-count mode: sends a 2-byte little-endian count of sb.available(),
 *    then clears the flag.  Activated by calling setReturnAvailable().
 *
 * BUG FIX: original had `if (i >= 30) break;` — hard-coded 30 with a comment
 * saying "32 bytes".  Replaced with the named constant WIRE_MAX_SEND_BYTES
 * (defined in the header) so the limit is in one place and the comment matches.
 *
 * BUG FIX: "ReturnAvalible" renamed to "returnAvailable" (typo + encapsulation).
 */
void WireHelper::requestEventProcessor(void)
{
    int num = sb.available();

    if (returnAvailable)
    {
        // Send the byte count as a 2-byte little-endian word, then clear flag.
        returnAvailable = false;
        wire->write((uint8_t)(num & 0xFF));
        wire->write((uint8_t)((num >> 8) & 0xFF));
        return;
    }

    // Send up to WIRE_MAX_SEND_BYTES bytes from the staging buffer.
    for (int i = 0; i < num && i < WIRE_MAX_SEND_BYTES; i++)
    {
        wire->write(sb.read());
    }
}

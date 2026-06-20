#ifndef WIREHELPER_H_
#define WIREHELPER_H_

/**
 * @file WireHelper.h
 * @brief I²C (TWI) slave-side read/write helper for Arduino systems.
 *
 * WireHelper provides two groups of functionality:
 *
 *  1. **Read helpers** — typed wrappers around Wire.read() for use inside
 *     an I²C `receiveEvent` handler.  Each function checks Wire.available()
 *     before every byte read and returns false / -1 on underflow rather than
 *     blocking or reading stale data.
 *
 *  2. **Send helpers** — typed wrappers that stage outgoing bytes into an
 *     internal SerialBuffer.  The staged bytes are flushed to the master by
 *     calling requestEventProcessor() from the I²C `requestEvent` handler.
 *
 * All multi-byte values are transferred **little-endian** (LSB first), which
 * matches the natural byte order of ARM and AVR Cortex-M cores.
 *
 * Wire width contract
 * -------------------
 * Functions that send or receive fixed-width quantities use explicitly sized
 * types (int16_t, int32_t, uint8_t, float) to prevent silent truncation or
 * sign-extension bugs when the same code runs on both 16-bit (AVR) and 32-bit
 * (SAMD, Teensy, ESP32) platforms.
 *
 * Usage sketch
 * ------------
 * @code
 *   WireHelper wh;
 *
 *   void receiveEvent(int n) {
 *       float f;
 *       if (wh.ReadFloat(&f)) handleValue(f);
 *   }
 *
 *   void requestEvent(void) {
 *       wh.requestEventProcessor();
 *   }
 *
 *   void setup() {
 *       Wire.begin(I2C_ADDR);
 *       Wire.onReceive(receiveEvent);
 *       Wire.onRequest(requestEvent);
 *       wh.SendFloat(3.14f);   // stage a value for the next master read
 *   }
 * @endcode
 */

#include <Arduino.h>        // Corrected case (Linux FS is case-sensitive)
#include <Wire.h>
#include <SerialBuffer.h>

/** Maximum bytes transmitted per requestEvent (I²C packet limit). */
static const int WIRE_MAX_SEND_BYTES = 30;

class WireHelper
{
public:
    // -----------------------------------------------------------------------
    // Construction / configuration
    // -----------------------------------------------------------------------

    /**
     * @brief Construct a WireHelper bound to the default Wire interface.
     *
     * Call SetInterface() to redirect to Wire1, Wire2, etc.
     */
    WireHelper();

    /**
     * @brief Redirect to a different TwoWire bus (e.g. Wire1).
     * @param w  Pointer to the TwoWire instance to use.  Must not be NULL.
     */
    void SetInterface(TwoWire *w);

    // -----------------------------------------------------------------------
    // Read helpers  (call from inside a receiveEvent handler)
    //
    // All functions return false / -1 if the required bytes are not available
    // in the Wire receive buffer.  No blocking occurs.
    // All multi-byte reads are little-endian (LSB first).
    // -----------------------------------------------------------------------

    /**
     * @brief Read one unsigned byte.
     * @return The byte value [0, 255], or -1 if no byte is available.
     */
    int ReadUnsignedByte(void);

    /**
     * @brief Read one signed byte into @p b.
     * @return true on success; false if no byte is available.
     */
    bool ReadByte(int8_t *b);

    /**
     * @brief Read one boolean byte into @p b.
     * @return true on success; false if no byte is available.
     */
    bool ReadBool(bool *b);

    /**
     * @brief Read a little-endian unsigned 16-bit word.
     *
     * Unlike the other multi-byte reads this function returns the value
     * directly.  A return value of -1 (cast to int) indicates underflow;
     * use Read16bitInt() if you need a clean bool/out-param API.
     *
     * @return Value in [0, 65535], or -1 on underflow.
     */
    int ReadUnsignedWord(void);

    /**
     * @brief Read a little-endian signed 16-bit integer into @p val.
     * @return true on success; false if fewer than 2 bytes are available.
     */
    bool Read16bitInt(int16_t *val);

    /**
     * @brief Read a little-endian signed 32-bit integer into @p val.
     *
     * Uses byte-pointer casting to avoid undefined-behaviour left-shifts on
     * 16-bit platforms (AVR), where `int` is only 16 bits wide.
     *
     * @return true on success; false if fewer than 4 bytes are available.
     */
    bool ReadInt(int32_t *val);

    /**
     * @brief Read a 4-byte IEEE 754 float into @p fval (little-endian byte order).
     * @return true on success; false if fewer than 4 bytes are available.
     */
    bool ReadFloat(float *fval);

    // -----------------------------------------------------------------------
    // Send helpers  (stage bytes for the next requestEvent)
    //
    // These functions write typed values into the internal send buffer (sb).
    // The bytes are held there until requestEventProcessor() is called from
    // the requestEvent handler, at which point they are sent to the master.
    // All multi-byte sends are little-endian (LSB first).
    // -----------------------------------------------------------------------

    /** @brief Stage one boolean byte for transmission. */
    void SendBool(bool bval);

    /** @brief Stage one unsigned byte for transmission. */
    void SendByte(uint8_t bval);

    /**
     * @brief Stage a little-endian 16-bit signed integer (2 bytes).
     *
     * Parameter is int16_t (not int) to make the 2-byte width contract
     * explicit and prevent silent truncation on 32-bit platforms.
     */
    void SendWord(int16_t ival);

    /**
     * @brief Stage the low 3 bytes of @p ival as a little-endian 24-bit value.
     *
     * Only bytes [0], [1], and [2] of the int32_t representation are sent.
     * The caller must ensure the value fits in 24 bits (i.e. is in the range
     * [-8388608, 16777215]); the most-significant byte is silently dropped.
     */
    void SendInt24(int32_t ival);

    /**
     * @brief Stage a 4-byte IEEE 754 float for transmission (little-endian).
     */
    void SendFloat(float fval);

    // -----------------------------------------------------------------------
    // Request-event handler
    // -----------------------------------------------------------------------

    /**
     * @brief Flush the staged send buffer to the I²C master.
     *
     * Call this from the Wire onRequest (requestEvent) handler.  Sends up to
     * WIRE_MAX_SEND_BYTES (30) bytes from the internal send buffer.
     *
     * If setReturnAvailable() was called before this event, the function
     * instead sends a 2-byte little-endian count of the bytes waiting in the
     * send buffer, then clears the flag.
     */
    void requestEventProcessor(void);

    /**
     * @brief Request that the next requestEvent sends the available-byte count
     *        instead of payload data.
     *
     * When set, the next call to requestEventProcessor() transmits a 2-byte
     * little-endian value equal to sb.available(), then clears this flag
     * automatically.
     */
    void setReturnAvailable(void) { returnAvailable = true; }

    /** @brief Return true if the available-count mode is pending. */
    bool getReturnAvailable(void) const { return returnAvailable; }

    SerialBuffer  sb;              ///< Internal byte-staging send buffer
private:
    TwoWire      *wire;            ///< Active I²C bus (default: &Wire)
    bool          returnAvailable; ///< When true, next request sends byte count
};

#endif // WIREHELPER_H_

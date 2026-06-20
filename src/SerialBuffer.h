/**
 * @file    SerialBuffer.h
 * @brief   Circular (ring) buffer implementing the Arduino Stream interface,
 *          with optional I2C (TwoWire) flushing support.
 *
 * SerialBuffer provides a fixed-size, interrupt-safe circular buffer that can
 * be used as a drop-in Stream wherever buffered serial-style I/O is needed.
 * When constructed with a TwoWire instance and a target I2C address, calls to
 * flush() (and automatic mid-write flushes when the buffer reaches half
 * capacity) will drain buffered bytes over I2C in Wire-compatible 30-byte
 * chunks — the maximum payload size for a single I2C transmission on most
 * Arduino-compatible Wire implementations.
 *
 * Thread / ISR safety
 * -------------------
 * Modifications to `sbsize` are bracketed by noInterrupts()/interrupts() so
 * that the size counter remains consistent when read() or write() is called
 * from both foreground code and ISRs.  `head` and `tail` are only ever
 * touched from a single context each (writer / reader respectively), so they
 * do not require additional protection.
 *
 * Usage example
 * -------------
 *   SerialBuffer sb;
 *
 *   // Stand-alone in-memory buffer:
 *   sb.begin();
 *
 *   // Or with I2C auto-flush to address 0x42:
 *   sb.begin(&Wire, 0x42);
 *
 *   sb.write("hello", 5);
 *   sb.flush();            // sends bytes over I2C (if configured)
 */

#ifndef SERIAL_BUFFER_H
#define SERIAL_BUFFER_H

#include <Arduino.h>
#include <inttypes.h>
#include <Wire.h>

/** Total capacity of the circular buffer in bytes. */
#define SB_SIZE 512

/**
 * Maximum number of bytes sent in a single I2C transmission.
 * The Wire library limits one transmission to 32 bytes; reserving 2 bytes for
 * the address leaves 30 bytes of usable payload.
 */
#define SB_I2C_CHUNK 30

class SerialBuffer : public Stream
{
public:
    SerialBuffer();
    ~SerialBuffer();

    /**
     * Initialise the buffer for stand-alone (non-I2C) use.
     * Resets all pointers and disables the I2C channel.
     */
    void begin();

    /**
     * Initialise the buffer with an I2C channel for auto-flushing.
     *
     * @param twi  Pointer to an initialised TwoWire instance (e.g. &Wire).
     * @param add  7-bit I2C target address.
     */
    void begin(TwoWire *twi, uint8_t add);

    /**
     * Discard all buffered data and reset the buffer to an empty state.
     * Does NOT flush pending bytes to I2C before clearing.
     */
    void clear();

    // ------------------------------------------------------------------ //
    // Stream / Print interface                                             //
    // ------------------------------------------------------------------ //

    /** Write a single byte.  Returns 1 on success, 0 if the buffer is full. */
    virtual size_t write(uint8_t byte) override;

    /**
     * Write up to @p size bytes from @p data.
     * Returns the number of bytes actually written.  If the buffer becomes
     * half-full during the write, an intermediate flush() is performed
     * automatically (only when an I2C channel is configured).
     */
    virtual size_t write(const uint8_t *data, size_t size) override;

    /** Returns the number of bytes currently available for reading. */
    virtual int available() override;

    /**
     * Read and remove one byte from the buffer.
     * Returns the byte value (0–255), or -1 if the buffer is empty.
     */
    virtual int read() override;

    /**
     * Return the next byte without removing it from the buffer.
     * Returns the byte value (0–255), or -1 if the buffer is empty.
     */
    virtual int peek() override;

    /**
     * Transmit buffered bytes over I2C in SB_I2C_CHUNK-byte chunks.
     * If no I2C channel is configured, or the buffer is empty, this is a
     * no-op.
     */
    virtual void flush() override;

private:
    uint8_t  buf[SB_SIZE]; ///< Backing store for the circular buffer.
    uint16_t head;         ///< Write index (next slot to be written).
    uint16_t tail;         ///< Read  index (next slot to be read).
    uint16_t sbsize;       ///< Number of bytes currently in the buffer.
    TwoWire *wire;         ///< I2C channel; NULL when not configured.
    uint8_t  twiadd;       ///< I2C target address.
};

#endif // SERIAL_BUFFER_H

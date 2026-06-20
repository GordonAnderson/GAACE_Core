/**
 * @file  SerialBuffer.cpp
 * @brief Implementation of SerialBuffer — see SerialBuffer.h for full docs.
 */

#include "SerialBuffer.h"

// -------------------------------------------------------------------------- //
// Construction / initialisation                                               //
// -------------------------------------------------------------------------- //

SerialBuffer::SerialBuffer()
{
    // Member variables are initialised by begin(); nothing to do here.
}

SerialBuffer::~SerialBuffer()
{
    // No heap allocations to release.
}

void SerialBuffer::begin()
{
    head   = 0;
    tail   = 0;
    sbsize = 0;
    wire   = NULL;
}

void SerialBuffer::begin(TwoWire *twi, uint8_t add)
{
    head    = 0;
    tail    = 0;
    sbsize  = 0;
    wire    = twi;
    twiadd  = add;
}

void SerialBuffer::clear()
{
    // Atomically reset all three counters so that concurrent readers (ISRs)
    // always see a consistent empty state.
    noInterrupts();
    head   = 0;
    tail   = 0;
    sbsize = 0;
    interrupts();
}

// -------------------------------------------------------------------------- //
// Stream write interface                                                      //
// -------------------------------------------------------------------------- //

size_t SerialBuffer::write(uint8_t byte)
{
    // Proactively flush when the buffer is half-full so the caller is less
    // likely to see a full-buffer failure on the next write.
    if (sbsize > SB_SIZE / 2)
    {
        flush();
    }

    if (sbsize == SB_SIZE)
    {
        return 0; // Buffer is full; byte dropped.
    }

    buf[head] = byte;
    head = (head + 1) % SB_SIZE; // Wrap using modulo to stay in bounds.

    noInterrupts();
    sbsize++;
    interrupts();

    return 1;
}

size_t SerialBuffer::write(const uint8_t *data, size_t size)
{
    if (size == 0)
    {
        return 0;
    }

    // Proactively flush when the buffer is already half-full.
    if (sbsize > SB_SIZE / 2)
    {
        flush();
    }

    if (sbsize == SB_SIZE)
    {
        return 0; // Buffer is full; nothing written.
    }

    size_t written = 0;

    for (size_t i = 0; i < size; i++)
    {
        if (sbsize == SB_SIZE)
        {
            break; // No space left; stop early.
        }

        buf[head] = data[i];
        head = (head + 1) % SB_SIZE;

        // Update sbsize atomically inside the loop so that an ISR reading
        // available() always sees a consistent count. interrupts() is called
        // unconditionally here — previously it was skipped on early exit,
        // which could leave interrupts permanently disabled (bug fix).
        noInterrupts();
        sbsize++;
        interrupts();

        written++;
    }

    return written;
}

// -------------------------------------------------------------------------- //
// Stream read interface                                                       //
// -------------------------------------------------------------------------- //

int SerialBuffer::available()
{
    return sbsize;
}

int SerialBuffer::read()
{
    if (sbsize == 0)
    {
        return -1; // Buffer is empty.
    }

    uint8_t byte = buf[tail];
    tail = (tail + 1) % SB_SIZE;

    noInterrupts();
    sbsize--;
    interrupts();

    return byte;
}

int SerialBuffer::peek()
{
    // Return the next byte without advancing the tail pointer.
    // Returns -1 (consistent with read()) when the buffer is empty.
    if (sbsize == 0)
    {
        return -1;
    }

    return buf[tail];
}

// -------------------------------------------------------------------------- //
// Flush to I2C                                                                //
// -------------------------------------------------------------------------- //

void SerialBuffer::flush()
{
    // Nothing to do if there is no I2C channel or no buffered data.
    if ((wire == NULL) || (available() == 0))
    {
        return;
    }

    // Drain the buffer in SB_I2C_CHUNK-byte transmissions.  The Wire library
    // limits a single transaction to 32 bytes; SB_I2C_CHUNK (30) leaves two
    // bytes of headroom for the I2C address overhead used internally by Wire.
    while (available() > 0)
    {
        wire->beginTransmission(twiadd);

        for (int i = 0; i < SB_I2C_CHUNK; i++)
        {
            if (available() == 0)
            {
                break;
            }
            wire->write(read());
        }

        wire->endTransmission();
    }
}

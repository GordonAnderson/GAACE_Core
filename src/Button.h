/*
 * Button — small debounced digital-button driver.
 *
 * Dual-build: compiles on pure STM32Cube (HAL GPIO + HAL_GetTick) AND on
 * Arduino (pinMode/digitalRead/millis) from the same source, selected by the
 * ARDUINO macro. The debounce LOGIC is identical on both — only the pin I/O
 * and time source differ.
 *
 * MIT licensed.
 */

#ifndef BUTTON_H
#define BUTTON_H

#include "gaace_compat.h"     // dual-build: pulls <Arduino.h> or Cube CMSIS bits

#if !defined(ARDUINO)
#include "stm32h7xx_hal.h"    // HAL GPIO types on the Cube path
#endif

/**
 * @file Button.h
 * @brief Debounced digital button driver.
 *
 * Provides edge-detection (pressed / released / toggled) on top of a simple
 * time-based debounce filter. The debounce window uses unsigned elapsed-time
 * arithmetic, so it is immune to the millis()/HAL_GetTick() 32-bit rollover.
 *
 * IMPORTANT: call read() once per loop iteration. pressed(), released(), and
 * toggled() all operate on the snapshot captured by the most recent read().
 *
 * Active-low wiring is assumed (button connects the pin to GND; internal
 * pull-up enabled). PRESSED = pin low, RELEASED = pin high.
 *
 * Construction differs by platform:
 *   Arduino:   Button btn(7);                       // integer pin
 *   STM32Cube: Button btn(GPIOB, GPIO_PIN_7);       // port + pin
 */
class Button
{
public:
    // -----------------------------------------------------------------------
    // State constants (active-low)
    // -----------------------------------------------------------------------
#if defined(ARDUINO)
    static const uint8_t PRESSED  = LOW;
    static const uint8_t RELEASED = HIGH;
#else
    static const uint8_t PRESSED  = 0;   // pin low  = pressed
    static const uint8_t RELEASED = 1;   // pin high = released
#endif

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------
#if defined(ARDUINO)
    explicit Button(uint8_t pin, uint16_t debounce_ms = 100);
#else
    explicit Button(GPIO_TypeDef *port, uint16_t pin, uint16_t debounce_ms = 100);
#endif

    void begin();

    bool read();

    bool hasChanged();
    bool pressed();
    bool released();
    bool toggled();

private:
#if defined(ARDUINO)
    uint8_t       _pin;
#else
    GPIO_TypeDef *_port;
    uint16_t      _pin;
#endif
    uint16_t _debounceMs;
    bool     _state;
    uint32_t _lastChangeTime;
    bool     _hasChanged;

    bool     rawRead();
    uint32_t nowMs();
    void     configurePin();
};

#endif // BUTTON_H

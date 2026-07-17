/*
 * Button — debounced digital-button driver (dual-build implementation).
 *
 * The debounce logic is shared; three small private primitives (rawRead,
 * nowMs, configurePin) isolate the platform differences.
 *
 * MIT licensed.
 */

#include "Button.h"

// =============================================================================
//  Platform primitives
// =============================================================================

#if defined(ARDUINO)

Button::Button(uint8_t pin, uint16_t debounce_ms)
    : _pin(pin)
    , _debounceMs(debounce_ms)
    , _state(true)              // RELEASED default; begin() seeds real state
    , _lastChangeTime(0)
    , _hasChanged(false)
{
}

void Button::configurePin() { pinMode(_pin, INPUT_PULLUP); }
bool Button::rawRead()      { return (bool)digitalRead(_pin); }
uint32_t Button::nowMs()    { return millis(); }

#else  // ---- pure STM32Cube ----

Button::Button(GPIO_TypeDef *port, uint16_t pin, uint16_t debounce_ms)
    : _port(port)
    , _pin(pin)
    , _debounceMs(debounce_ms)
    , _state(true)              // RELEASED default; begin() seeds real state
    , _lastChangeTime(0)
    , _hasChanged(false)
{
}

void Button::configurePin()
{
    // Input with pull-up (active-low button). Port clock assumed enabled by
    // board init.
    GPIO_InitTypeDef g = {0};
    g.Pin   = _pin;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(_port, &g);
}

bool Button::rawRead()
{
    return HAL_GPIO_ReadPin(_port, _pin) == GPIO_PIN_SET;  // true = high = released
}

uint32_t Button::nowMs() { return HAL_GetTick(); }

#endif

// =============================================================================
//  Shared logic (identical on both platforms)
// =============================================================================

void Button::begin()
{
    configurePin();
    _state = rawRead();          // seed with real hardware state (no boot glitch)
}

bool Button::read()
{
    bool currentPin = rawRead();

    if (currentPin != _state)
    {
        uint32_t elapsed = nowMs() - _lastChangeTime;   // rollover-safe
        if (elapsed >= _debounceMs)
        {
            _lastChangeTime = nowMs();
            _state          = currentPin;
            _hasChanged     = true;
        }
    }
    return _state;
}

bool Button::hasChanged()
{
    if (_hasChanged) { _hasChanged = false; return true; }
    return false;
}

bool Button::pressed()  { return (_state == (bool)PRESSED)  && hasChanged(); }
bool Button::released() { return (_state == (bool)RELEASED) && hasChanged(); }
bool Button::toggled()  { return hasChanged(); }

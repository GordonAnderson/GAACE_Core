# GAACE_Core

GAACE_Core (GAA Custom Electronics Core Extensions) is a collection of reusable C++ libraries that form the infrastructure layer for embedded firmware projects built on the GAACE framework. Designed to be architecture-agnostic, these libraries have been successfully deployed across SAMD21, SAMD51, SAM3X8 (Arduino Due), ESP32, and Teensy platforms.

This repository is structured as a [PlatformIO](https://platformio.org/) library:
the framework source lives in [src/](src/) and is consumed by the application
firmware projects that depend on it.

## Modules

| Module | File(s) | Purpose |
| --- | --- | --- |
| commandProcessor | [commandProcessor.h/.cpp](src/commandProcessor.h) | Serial command dispatcher — parses comma-delimited ASCII commands from multiple streams and routes them to typed handlers or function callbacks. |
| ringBuffer | [RingBuffer.h/.cpp](src/RingBuffer.h) | Circular character buffer with token, line, and delimiter-aware read operations; underpins commandProcessor input. |
| charAllocate | [charAllocate.h/.cpp](src/charAllocate.h) | Lightweight arena allocator for short-lived char arrays on constrained heaps; avoids heap fragmentation. |
| debug | [debug.h/.cpp](src/debug.h) | Plug-in command group exposing memory inspection, ADC/DAC raw access, GPIO control, CPU temperature, UUID, uptime, and software reset via the command processor. |
| Button | [Button.h/.cpp](src/Button.h) | Debounced digital input abstraction with pressed / released / toggled edge-detection helpers. |
| Devices | [Devices.h/.cpp](src/Devices.h) | Hardware-abstraction helpers for ADC/DAC channels with linear calibration (m, b), plus low-level SPI/I2C drivers for AD5592R, AD5593R, DAC8571, and MCP4725. |
| WireHelper | [WireHelper.h/.cpp](src/WireHelper.h) | I2C (Wire) receive/transmit helper: typed readers (bool, byte, word, int, float) and buffered typed senders for I2C slave request-event handlers. |
| AtomicBlock | [AtomicBlock.h](src/AtomicBlock.h) | Portable, RAII-style interrupt critical-section wrapper for AVR, ARM Cortex-M, PIC32, and AVR32. Third-party (GNU GPL v3) — see header for attribution. |
| Errors | [Errors.h](src/Errors.h) | Typed `ErrorCode` enum (`ERR_*`) used across GAACE-based firmware projects. |
| FlashFS | [FlashFS/](src/FlashFS/) | Embedded FAT filesystem layer: FatFs configured for minimal footprint, with Adafruit FlashTransport targeting QSPI/SPI flash and ESP32/RP2040 onboard flash. |

All modules depend only on the Arduino core (`Arduino.h`) unless noted otherwise:

- `Devices.h` additionally requires `SPI.h` and `Wire.h`.
- `WireHelper.h` requires a `SerialBuffer` dependency (not included in this package).
- `FlashFS` requires `Adafruit_SPIFlash` (and transitively `SdFat - Adafruit Fork`). Because
  of this extra dependency, `FlashFS/` is excluded from the library's default build
  (see `library.json`'s `build.srcFilter`) so that projects which don't use it aren't
  forced to resolve it. Projects that do use FlashFS must add it back via their own
  `lib_extra_dirs`/`build_src_filter` plus the `Adafruit_SPIFlash` dependency.

## Documentation

Full API documentation and a v1.0 → v1.1.0 migration guide are included in this
repository:

- [GAACE_API_Reference_v1.1.0.docx](GAACE_API_Reference_v1.1.0.docx) — library description, API reference, and usage examples for every module, plus the MIPS I2C integration protocol.
- [GAACE_Update_Checklist.docx](GAACE_Update_Checklist.docx) — checklist of breaking changes and behavioral fixes when migrating an application from v1.0 to v1.1.0.

## Installation

### PlatformIO (recommended)

This repo is laid out as a library with one example project. The root
[platformio.ini](platformio.ini) builds the [examples/DiagnosticDemo](examples/DiagnosticDemo)
sketch against the `src/` library:

```ini
[platformio]
default_envs = adafruit_feather_m0
src_dir = examples/DiagnosticDemo/src

[env:adafruit_feather_m0]
platform = atmelsam
board = adafruit_feather_m0
framework = arduino
lib_deps =
    symlink://.
```

To use GAACE_Core from another PlatformIO project, add it as a library
dependency (e.g. via `lib_deps` pointing at this repo, or a `symlink://` path
during local development as the example does):

```ini
lib_deps =
    symlink://../../GAACE_Core
```

### Arduino IDE

Copy the contents of `src/` into a folder named `GAACE_Core` under your Arduino
`libraries` directory, then restart the IDE and include the headers you need —
there is no single umbrella include.

## Example

[examples/DiagnosticDemo](examples/DiagnosticDemo) wires up `commandProcessor`
and the `debug` module on USB serial, plus one custom command (`BLINK`, `?LED`)
to show how application-specific commands are registered alongside the
built-ins. Flash it to an Adafruit Feather M0, connect a serial terminal at
115200 baud, and try `GCMDS`, `UPTIME`, `RAM`, `CPUTEMP`, `UUID`, `SLED,TRUE`,
`GLED`, or `BLINK,5`.

## Minimal usage

```cpp
#include <commandProcessor.h>

commandProcessor cp;

static void setVoltage(void) {
  float v;
  if (!cp.checkExpectedArgs(1)) return;
  if (cp.getValue(&v, 0.0, 100.0)) {
    setHV(v); cp.sendACK();
  } else cp.sendNAK();
}

static Command myCmds[] = {
  {"SETVOLT", CMDfunction, 1, (void*)setVoltage, NULL, "Set HV in volts"},
  {NULL}
};
static CommandList myList = {myCmds, NULL};

void setup() {
  Serial.begin(115200);
  cp.registerStream(&Serial);
  cp.registerCommands(&myList);
}

void loop() {
  cp.processStreams();
  cp.processCommands();
}
```

See the API reference document above for the full set of modules, command
table options, and the I2C/MIPS integration protocol.

## License

This project is licensed under the GNU General Public License v3.0 — see
[LICENSE](LICENSE) for the full text. GPLv3 was chosen because the bundled
[AtomicBlock.h](src/AtomicBlock.h) is third-party code by Christopher Andrews
already distributed under GPLv3, so the package as a whole is GPLv3.

## Author

Gordon Anderson — gaa@gaa-ce.com

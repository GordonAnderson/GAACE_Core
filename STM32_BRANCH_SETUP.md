# Creating the `stm32` branch of GAACE_Core (PlatformIO / VS Code Git GUI)

You have the GAACE_Core repo local in PlatformIO and use the built-in Git GUI.
No terminal needed. This drops in the ported files as the first commit on a new
`stm32` branch.

## Step 0 — start clean on main

- Make sure the Source Control panel shows **no uncommitted changes** and you're
  on `main` (branch name shown bottom-left in the VS Code status bar).
- Optional but tidy: pull latest `main` first (Source Control "..." menu → Pull).

## Step 1 — create the `stm32` branch

- Click the branch name (**main**) in the bottom-left status bar.
- Choose **"Create new branch..."**, name it `stm32`, confirm.
- The status bar should now read `stm32`. You are branched off main; nothing has
  changed yet.

## Step 2 — drop in the files

Copy the files from this handoff into the repo, replacing where they already
exist. Layout matches the repo's `src/` folder exactly.

**Modified (overwrite the existing files in `src/`):**
```
src/commandProcessor.cpp
src/commandProcessor.h
src/RingBuffer.cpp
src/RingBuffer.h
src/charAllocate.cpp
src/charAllocate.h
src/Button.cpp
src/Button.h
src/debug.cpp
src/debug.h
```

**New (add to `src/`):**
```
src/gaace_compat.h
src/GStream.h
src/GArduinoStream.h
src/GHal.h
```

**Also replace (repo root):**
```
library.json          (framework field opened to "*"; version -> 1.1.0-stm32)
```

## Step 3 — review the diff (recommended)

In the Source Control panel you'll see the 10 modified files, the 4 new files,
and library.json. Click a couple to eyeball the diff — the big ones are
`commandProcessor.cpp` (String removed) and `debug.cpp` (H7 branches + GHal).

## Step 4 — commit

- Stage all changes (the "+" on each file, or stage-all).
- Commit message (suggested):

```
stm32: make GAACE_Core Arduino-optional (pure STM32Cube support)

Add a GStream/Print shim so the command processor, RingBuffer, charAllocate,
Button, and debug build on bare STM32Cube (HAL/LL) as well as Arduino, selected
by the ARDUINO macro.

- commandProcessor/RingBuffer: remove Arduino String (heap-free); use GStream*.
- GStream.h: own Stream/Print base. GArduinoStream.h: one bridge for all Arduino
  platforms. gaace_compat.h: dual-build switch.
- Button: STM32 (port,pin) ctor alongside Arduino integer-pin ctor.
- debug: portable core + H7 branches (RESET/UUID/CPUTEMP); numbered pin/analog
  commands route through an optional project-supplied GHal (GHal.h).
- library.json: frameworks "*" so PlatformIO allows non-Arduino builds.

Not yet ported (still Arduino-only on this branch): Devices, WireHelper,
SerialBuffer, WireServer, FlashFS board config. AtomicBlock/Errors unchanged.

Compiles clean both ways (Arduino + pure-Cube) from identical sources; command
dispatch, Button, and debug verified off-target.
```

## Step 5 — publish the branch

- Source Control "..." menu → **Push** (or the "Publish Branch" prompt VS Code
  shows for a new local branch).
- This creates `origin/stm32` on GitHub without touching `main`.

## After pushing — how you'll use it

- **Rev 6.0 (pure Cube) project:** point its `lib_deps` / git submodule at the
  `stm32` branch of GAACE_Core.
- **Regression test on existing hardware:** point an existing SAMD/ESP32/Teensy
  Arduino project at the `stm32` branch, wrap its `Serial` in a `GArduinoStream`
  (see BRANCH_AND_STRUCTURE.md), rebuild, confirm behavior unchanged. This is the
  low-friction validation before anything merges to `main`.

## Notes / gotchas

- **`ARDUINO` is set globally by PlatformIO** on Arduino-framework builds, so all
  translation units see it — the dual-build switch keys off that. On a Cube
  (framework = stm32cube, or a bare CMSIS) project, `ARDUINO` is absent and the
  Cube path compiles. Nothing for you to define manually.
- The still-unported modules (`Devices`, `WireHelper`, `SerialBuffer`,
  `WireServer`) still `#include <Arduino.h>`. They'll compile on the Arduino
  regression build but NOT in a pure-Cube project yet. For the first Rev 6.0
  bring-up you won't reference them; they get ported onto this same `stm32`
  branch next.
- `library.json` `srcFilter` still excludes `FlashFS` from the default build
  (unchanged) — that's pulled in explicitly when you wire up QSPI config storage.

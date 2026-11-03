# Offset Lab

An experimental [Canvas / Sky Modloader](https://github.com/books4jy-art) mod for exploring the game library of
**Sky: Children of the Light 0.35.2** (Android, arm64), in the **Sky Mesh Lab** theme
([sky-mesh-explorer-labs](https://github.com/books4jy-art/sky-mesh-explorer-labs)): near-black, white text, thin
borders, sky blue accents.

It exists to satisfy curiosity: try things and see what they do. Expect crashes; use it on a private server.

## Install

Download `libOffsetLab.so` from the [latest release](../../releases/latest) and import it into Canvas as a mod.

## Tabs

| Tab | What it does |
|---|---|
| **Game functions** | Every type the game registers by name (2,751). Only **events** (types that descend from the game's Event type) can be fired; asking the game to fire anything else makes it abort, so Fire is off for them and *Events only* is on by default. Each is shown with a readable title ("Avatar set inner glow") and the game's name under it. **What it does**: the game's own notes for it and the developers' description of each setting, where they wrote one. **Settings**: each gets the control that fits its type in the game (toggle, slider with typing, whole number, text, x/y/z, colour picker, a choice of the game's own options, or a Lua object), starting at the game's default; only changed settings are sent. Under each setting, **Accepts** says what it takes: the game's own valid range where it gives one (`Tool_MinMaxValues`, 87 settings; sliders and typing are held to it), otherwise its type and a suggested range. Object settings (markers, NPCs, Float nodes…) refuse numbers and text, since the game would use them as objects and crash. Where the game keeps no type for a setting, it's guessed from the name and labelled so. **Fire**, **Fire on change**, **Stop**, **Back / Forward / Recent**. Offsets are under *Offsets*. |
| **Lua** | Run any Lua in the game. The result is the game's code (0 = no error). |
| **Memory** | Read-only view of any offset in libBootloader.so (or an absolute address): hex, text and the value as integer, float, double or pointer, with *follow pointer*. Reads never crash: unreadable memory shows `??`. |
| **Native** | The functions the game exports by name, read from its own symbol table. **Watch** counts calls per second and shows the last four arguments; *Watch any offset* takes a hex offset (it must be the start of a function). |
| **Log** | What the mod did. |

**Server types** (account, purchases, inventory, friends, requests): fireable only after turning on
*Server types*, which is off at every start, because they send requests to whatever server the game is connected to.

## How it works

- Nothing from the game is stored here: `src/generated/lab_data.inc` holds only **addresses** (offsets from the start
  of libBootloader.so) and a 32-bit checksum of each name. Names, setting names and notes are read from the running
  game; the checksum shows whether the read worked (on another game version the offsets are wrong and it says so).
- `tools/gen_offsets.py libBootloader.so` (Python 3, numpy, capstone) makes that file. It finds every call to the
  0.35.2 function that registers a type by name (`0x29B4E5C`), and each type's setting records: name, position in the
  event, type handler (which says toggle, number, text, vector, colour, choice or object) and attributes (default
  value, description, object type). Choices are the option names registered with the same type handler. It also
  records each type's parent (the class tree: a type's parent accessor returns the parent's type object), so the mod can
  tell events from everything else; parents not resolvable from the file are read at runtime from the type object
  (+0x30).
- Game addresses used: game object pointer `0x33A5E80`, its Lua state at `+0x20`, the game's Lua runner `0x29ADFA4`.

## Building

Android NDK r23c, CMake 3.22+, Ninja:

```sh
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Canvas provides Dear ImGui at runtime (`libciphered.so`). To link, the build makes a link-only stand-in from the same
Dear ImGui commit Canvas uses; it isn't shipped. GitHub Actions builds every push and publishes a release on `main`
when `config/config.json`'s version is new.

## Third-party code

- [Dear ImGui](https://github.com/ocornut/imgui) (commit d3c3514a, as in Canvas), MIT: `third_party/imgui`
- [Dobby](https://github.com/jmpews/Dobby), Apache-2.0: `third_party/dobby` (prebuilt arm64 static library)

Sky: Children of the Light is © thatgamecompany. This mod isn't affiliated with thatgamecompany or Canvas. Use at
your own risk.

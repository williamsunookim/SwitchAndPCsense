# SwitchAndPCsense

Experimental DualSense Bluetooth firmware for Raspberry Pi Pico W and Pico 2 W, using a shared codebase and separate board builds.

The current `integrated-0.4.4-mode-led` build switches between **PC USB (DualSense)** and **Switch Bluetooth** output. Double-click the Pico's BOOTSEL button to toggle; the selected mode is saved across power cycles. In Switch mode, USB supplies power and diagnostics while input goes to the console over Bluetooth. In PC mode, Switch connections and console wake are disabled and USB carries DualSense game input and output.

The onboard LED identifies the selected mode in a repeating two-second cycle:
**one short blink = PC USB**, **two short blinks = Switch Bluetooth**. Pairing
uses the existing continuous fast blink; low battery uses its existing steady
on/off warning. Pairing-clear confirmation and battery warnings take priority
over the mode pattern. The LED identifies the selected mode, not whether the
console is connected. The LED-disable setting also disables the mode pattern.

Check or select the current mode from a connected PC:

```powershell
python tools/config/output_mode.py status
python tools/config/output_mode.py pc
python tools/config/output_mode.py switch
python tools/config/output_mode.py pair
```

`pair` starts Switch pairing only in Switch mode. BOOTSEL single-click still pairs the DualSense; double-click now changes modes. Mode changes briefly reconnect USB and stop previous haptics/audio before routing the new output. The default mode on a fresh configuration is PC.

## Supported boards

| Board | Processor | Clock | Build shortcut | Output |
|---|---|---|---|---|
| Raspberry Pi Pico W | RP2040 | 125 MHz | `build-pico-w.cmd` | `dist/Pico-W.uf2` |
| Raspberry Pi Pico 2 W | RP2350 | 150 MHz | `build-pico-2-w.cmd` | `dist/Pico-2-W.uf2` |

Both targets use the same controller, motion and haptics implementation. The Pico W build omits Opus processing; the Pico 2 W build includes the larger audio/core1 build path. This does not imply tested headset, microphone or speaker support in the current wireless mode.

## Build on Windows

Install Git, Python 3.9 or newer with pip, and either Visual Studio C++ Build Tools or native GCC/G++.

Double-click the shortcut for your board, or run from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico_w
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico2_w
```

The first build downloads missing SDK/compiler dependencies. Opus and WDL are fetched at the exact commits recorded in [`DEPENDENCIES.lock.json`](DEPENDENCIES.lock.json), rather than stored in this repository. Internet access is required when dependencies are missing. Later builds reuse the local copies.

```powershell
# Build and run all host tests
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico2_w -Test

# Explicitly prepare the full build environment
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Bootstrap

# Use a specific Python installation
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico_w -PythonExecutable C:\Python313\python.exe
```

Build caches are separate for each board under `firmware/build/`. Outputs are copied to `dist/`; the build does not automatically flash a connected board. The provided build entry points target Windows. Original upstream scripts for other platforms are retained as reference, not validated build instructions.

## Flash and pair

1. Hold **BOOTSEL** while connecting the Pico to your computer with a USB data cable.
2. Copy the matching UF2 from `dist/` to **RPI-RP2** (Pico W) or **RP2350** (Pico 2 W). The board restarts automatically.
3. Put the DualSense into pairing mode by holding **PS + Create**, then press the Pico's **BOOTSEL once** to start discovery.
4. On the Switch, open **Controllers → Change Grip/Order** and complete pairing.

Use the firmware for your exact board. This project remains experimental: successful compilation, host tests and USB diagnostics do not establish console pairing or gameplay compatibility.

## Validation and limitations

- Both board configurations have been built, and the portable host test suite has passed.
- Earlier Pico 2 W hardware checks confirmed boot, 150 MHz operation and a matching flash CRC; later repository reorganizations were verified by build and host tests.
- Actual console pairing, gameplay, latency and long-term dual-link Bluetooth stability still need hardware validation.
- Experimental Switch 2 sleep wake uses a captured beacon from an already paired Joy-Con 2 / Pro Controller 2. One-time setup changes the adapter Bluetooth identity and requires re-pairing; see the [wake setup guide](docs/SWITCH2_WAKE.md). Console wake is not hardware-verified.
- NFC/Amiibo is outside the implemented scope. PC input/output paths are restored, but game-specific haptics/audio still require hardware testing. Switch Bluetooth audio is not implemented.

See the [validation record](docs/VALIDATION.md), [limitations](docs/KNOWN_LIMITATIONS.md), and [host tools guide](tools/README.md). Most historical development notes remain in Korean.

## Repository layout

```text
build-pico-w.cmd           One-click Pico W build
build-pico-2-w.cmd         One-click Pico 2 W build
build.ps1                 Shared Windows build entry point
DEPENDENCIES.lock.json    Pinned dependency revisions
firmware/
  src/                    Shared firmware implementation
  boards/                 Board definitions
  cmake/                  Build helpers
  profiles/               Trigger configuration examples
  tests/                  Host tests and SDK stubs
  tools/                  Memory guard and protocol helper
  licenses/               Third-party license texts
  .deps/                  Downloaded Opus/WDL (ignored)
  build/                  Per-board build caches (ignored)
tools/
  bootstrap.py            Dependency setup
  config/                 Host configuration and bootloader tools
  diagnostics/            CRC, Bluetooth and motion diagnostics
  README.md               Host tools usage
  ...                     Downloaded build tools (ignored)
docs/
  README.md               Documentation index
  upstream/               Original documentation and build scripts
dist/                     Generated UF2 files (ignored)
```

Only project source, tests, profiles, scripts, documentation and license notices belong in Git. SDKs, compilers, external libraries, logs and generated firmware are ignored. Attach `dist/Pico-W.uf2` and `dist/Pico-2-W.uf2` to a GitHub Release when publishing a build; use Git commits and tags for source history.

## Credits and licenses

This repository derives from Pico-DualSense-Switch-Bridge and DS5Dongle and incorporates additional protocol and haptics work. See the [source review](docs/SOURCE_REVIEW.md), [third-party notices](firmware/THIRD_PARTY_NOTICES.md), and [upstream documentation](docs/upstream/README.md).

The repository [MIT license](LICENSE) does not replace file-specific or dependency licenses. In particular, `firmware/src/audio.cpp` carries MPL-2.0 terms. Full dependency notices are preserved in [`firmware/licenses/`](firmware/licenses/).

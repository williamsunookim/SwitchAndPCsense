# Host tools

Run commands from the repository root. `bootstrap.py` downloads the pinned build
and library dependencies and is called by `build.ps1` automatically.

## Configuration

```powershell
python -m pip install hidapi
python tools/config/trigger_config.py --help
python tools/config/config_tool.py --help
python tools/config/output_mode.py status
python tools/config/output_mode.py pc
python tools/config/output_mode.py switch
```

`config/reboot_bootsel.py` requests a reboot into the USB bootloader; it changes
the connected board state. Use the matching firmware before sending commands.
Configuration examples are in [`firmware/profiles/`](../firmware/profiles/).

BOOTSEL double-click also toggles PC USB / Switch Bluetooth and saves the mode.
`output_mode.py pair` starts console pairing in Switch mode. In PC mode, Switch
wireless output and Switch 2 wake are disabled.

## Diagnostics

```powershell
python tools/diagnostics/verify_board.py --board pico_w --uf2 dist/Pico-W.uf2
python tools/diagnostics/verify_board.py --board pico2_w --uf2 dist/Pico-2-W.uf2
python tools/diagnostics/wireless_status.py --help
```

The verifier checks the UF2 target, running firmware version, CPU clock and flash
CRC. A board must be connected and running a compatible build. Motion/link/gap
readers and `capture_motion.py` provide additional development diagnostics.

Downloaded SDKs, compilers, Python packages and tool caches in this directory are
local dependencies excluded by `.gitignore`.

## Switch 2 wake setup

`config/switch2_wake.py` captures a paired Switch 2 controller wake beacon, stores
the replay settings, requests a test burst, and reads diagnostics. See
[the setup guide](../docs/SWITCH2_WAKE.md) for the required one-time capture and
Bluetooth identity/re-pairing implications.

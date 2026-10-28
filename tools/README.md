# Host tools

Run commands from the repository root. `bootstrap.py` downloads the pinned build
and library dependencies and is called by `build.ps1` automatically.

## Configuration

```powershell
python -m pip install hidapi
python tools/config/trigger_config.py --help
python tools/config/config_tool.py --help
```

`config/reboot_bootsel.py` requests a reboot into the USB bootloader; it changes
the connected board state. Use the matching firmware before sending commands.
Configuration examples are in [`firmware/profiles/`](../firmware/profiles/).

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

# SwitchAndPCsense

Use a Bluetooth DualSense with a PC over USB or a Nintendo Switch over Bluetooth, using a Raspberry Pi Pico W or Pico 2 W.

**Current firmware: `integrated-0.4.5-pico-learn`**

- **PC USB mode:** presents a DualSense USB controller and forwards PC input/output.
- **Switch Bluetooth mode:** presents a Switch 1 Pro Controller over Bluetooth, with motion controls and HD Rumble converted to DualSense PCM haptics.
- **Switch 2 sleep wake:** learns a paired Joy-Con 2 / Pro Controller 2 wake beacon directly on the Pico. **PC Bluetooth is not required.**
- **Runtime mode switching:** double-click BOOTSEL; the selection survives power cycles.
- **Mode LED:** one short blink for PC, two short blinks for Switch, repeating about every two seconds.

This is experimental firmware. Pico 2 W boot, firmware CRC, mode persistence, BLE reception, wake-beacon learning and controller reconnection have been checked locally. These checks do not establish game compatibility, measured latency, long-term stability or reliable console wake across systems. See [validation](docs/VALIDATION.md).

## Hardware and downloads

| Board | MCU / clock | Firmware asset | Build shortcut |
|---|---|---|---|
| Raspberry Pi Pico W | RP2040 / 125 MHz | `Pico-W.uf2` | `build-pico-w.cmd` |
| Raspberry Pi Pico 2 W | RP2350 / 150 MHz | `Pico-2-W.uf2` | `build-pico-2-w.cmd` |

You also need a DualSense and a USB data cable. The Pico connects to the controller over Bluetooth in both modes. For Switch 2 wake setup, you need a Joy-Con 2 or Pro Controller 2 already paired to your own console.

Download the matching UF2 from [GitHub Releases](https://github.com/williamsunookim/SwitchAndPCsense/releases), when a release is available, or [build from source](#build-from-source-windows). Use the image for your exact board. Generated firmware is excluded from Git and distributed as release assets.

Both boards share the same input, motion and Switch haptics implementation. Pico W omits Opus speaker/microphone processing. Pico 2 W includes the larger audio build; its presence is not a claim of tested PC headset support. Switch Bluetooth audio is not implemented.

## Flash and pair the DualSense

1. Hold **BOOTSEL** while connecting the Pico to your computer over USB.
2. Copy the matching UF2 to **RPI-RP2** (Pico W) or **RP2350** (Pico 2 W). The board restarts automatically.
3. Hold **PS + Create** on the DualSense until its light flashes rapidly. Create is the small button to the left of the touchpad.
4. Press **BOOTSEL once** to start a 30-second controller search.

A fresh configuration starts in **PC USB mode**. In this mode the PC sees a DualSense USB device. Press PS to reconnect a previously paired controller; use PS + Create and BOOTSEL once when re-pairing is needed.

## Select PC or Switch mode

| BOOTSEL action | Result |
|---|---|
| One click | Start DualSense discovery; disconnect the current DualSense if connected |
| Two clicks | Toggle PC USB / Switch Bluetooth and save the selection |
| Three clicks | Enter BOOTSEL for reflashing |
| Hold about 1.5 seconds | Clear Bluetooth pairings; re-pair afterward |

| LED pattern | Meaning |
|---|---|
| One short blink, long pause | PC USB mode |
| Two short blinks, long pause | Switch Bluetooth mode |
| Continuous fast blinking | DualSense discovery |
| Regular on/off warning | Low controller battery |

Pairing-clear confirmation, low-battery and discovery indications take priority over the mode pattern. The LED indicates the selected mode, not a successful console connection. The LED-disable setting also disables the mode pattern.

To use the Switch, select Switch mode and open **Controllers ? Change Grip/Order** on the console. To explicitly start console pairing, use the command below. BOOTSEL single-click searches for the DualSense, not the console.

For USB commands, install Python 3.9+ and run from the repository root:

```powershell
python -m pip install hidapi
python tools/config/output_mode.py status
python tools/config/output_mode.py pc
python tools/config/output_mode.py switch
python tools/config/output_mode.py pair
```

`pair` requires Switch mode. A mode change briefly reconnects USB and clears previous haptics/audio state. In Switch mode, USB supplies power and diagnostics; game input is routed over Bluetooth. In PC mode, console connection attempts and console wake are disabled.

## Set up Switch 2 wake once

Keep the Pico connected to the PC by USB. **The Pico receives the beacon itself; the PC does not need Bluetooth or `bleak`.**

```powershell
python -m pip install hidapi
python tools/config/output_mode.py pc
python tools/config/switch2_wake.py learn
```

During the **30-second learning window**:

1. Put your Switch 2 into sleep mode.
2. Wake it with **HOME on one already-paired Joy-Con 2 / Pro Controller 2**. Keep it near the Pico.
3. Wait for **`saved`**. The Pico stores the settings automatically and the tool writes a backup to `dist/switch2-wake.json`. No separate `configure` command is needed.

Then:

1. Turn the original controller off.
2. Unplug and reconnect the Pico. Learning changes its Bluetooth identity, so **re-pair the DualSense using PS + Create and BOOTSEL once**.
3. Select Switch mode and pair the console again:

   ```powershell
   python tools/config/output_mode.py switch
   python tools/config/output_mode.py pair
   ```

4. Open **Controllers ? Change Grip/Order** on the console.

After setup, keep the Pico powered in Switch mode. Pressing PS on the DualSense reconnects it and requests console wake when the console link is absent. This targets **sleep**, not a fully powered-off console.

**Do not power on the original learned controller at the same time as the Pico:** they use the same Bluetooth address. Keep that controller registered on the console. Settings and pairings persist across power cycles, but clearing pairings, changing the learned identity or deleting the console's controller registration can require setup again.

Timeout, cancellation, multiple different donors and save failure leave existing settings intact. `candidate_seen` only means a beacon was detected; `saved` confirms storage. After reconnecting power, check `active: true` and `reboot_required: false`:

```powershell
python tools/config/switch2_wake.py status
```

See the [wake setup and troubleshooting guide](docs/SWITCH2_WAKE.md) for longer learning windows, backups and disabling wake.

## Troubleshooting and limitations

- **DualSense will not reconnect after learning:** the Pico address changed. Hold PS + Create and press BOOTSEL once. PS alone may still target the old pairing.
- **Switch is not connected:** check the selected mode, run `output_mode.py pair`, and open Change Grip/Order. A mode LED is not a connection indicator.
- **Learning finds no beacon:** use PC mode, move the Pico closer, and press HOME on a controller already registered to that console. Retry with `learn --seconds 60`.
- **Wake does not work:** check `active`, `reboot_required` and `hci_error`; keep Pico power on during sleep. Successful learning does not itself prove console wake.
- NFC/Amiibo, Switch Bluetooth headset/microphone/speaker audio and fully-off console power-on are outside the implemented scope.
- Game-specific PC haptics/audio, latency and sustained dual-link Bluetooth operation need further hardware testing. Pico W does not include the Pico 2 W Opus processing path.

Configuration and diagnostics are documented in [tools](tools/README.md). Historical design notes are indexed under [docs](docs/README.md); some remain in Korean. Archived upstream documentation describes earlier USB behavior and is not the current feature specification.

## Build from source (Windows)

Install Git, Python 3.9+ with pip, and Visual Studio C++ Build Tools or a native compiler supporting C++20.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico_w
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico2_w -Test
```

You can also double-click the board-specific CMD shortcut. The first build downloads missing SDK/compiler dependencies. Opus and WDL are fetched at the commits in [DEPENDENCIES.lock.json](DEPENDENCIES.lock.json). Internet access is needed when dependencies are missing; later builds reuse them.

Outputs are `dist/Pico-W.uf2` and `dist/Pico-2-W.uf2`, with separate caches under `firmware/build/`. Building does not flash the board automatically. See [release preparation](docs/RELEASING.md) before publishing binaries.

## Credits and licenses

Derived from Pico-DualSense-Switch-Bridge and DS5Dongle, with protocol work based on the sources credited in [source review](docs/SOURCE_REVIEW.md), [third-party notices](firmware/THIRD_PARTY_NOTICES.md) and the [wake protocol references](docs/SWITCH2_WAKE.md#protocol-references).

The repository [MIT license](LICENSE) does not replace file-specific or dependency licenses. In particular, `firmware/src/audio.cpp` carries MPL-2.0 terms. Preserve the modified source, notices and full [third-party license texts](firmware/licenses/) when distributing firmware.

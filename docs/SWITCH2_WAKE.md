# Switch 2 sleep wake

The Pico replays a wake beacon learned from a Joy-Con 2 or Pro Controller 2 already registered to your own Switch 2. PC Bluetooth is not required. This is an experimental sleep-wake feature, not fully-off console power-on.

## Requirements

- Firmware `integrated-0.4.5-pico-learn` or later on Pico W / Pico 2 W.
- A USB data connection to a PC running Python 3.9+.
- One Joy-Con 2 or Pro Controller 2 already paired to the target console.

Switch 1 Pro Controller pairing does not register a Switch 2 BLE wake identity. The implementation reuses a registered controller's beacon; it does not implement new Switch 2 USB/GATT registration.

## Learn directly on the Pico

Run from the repository root:

```powershell
python -m pip install hidapi
python tools/config/output_mode.py pc
python tools/config/switch2_wake.py learn
```

The Pico listens for 30 seconds. Put the console to sleep, then wake it with **HOME on one original paired controller**, close to the Pico. Wait until the window ends and the command reports **`saved`**.

A single valid donor is stored automatically on the Pico, with a backup written to `dist/switch2-wake.json`. A separate `configure` command is not needed. Multiple different donors, timeout, cancellation and flash-save failure leave existing settings intact.

To allow more time or restrict learning to a known donor address:

```powershell
python tools/config/switch2_wake.py learn --seconds 60
python tools/config/switch2_wake.py learn --source AA:BB:CC:DD:EE:FF
```

Use Ctrl+C or `python tools/config/switch2_wake.py cancel-learn` to cancel.

## Activate and re-pair

1. Turn the original donor controller off.
2. Unplug and reconnect Pico power. Its public Bluetooth address changes to the learned donor identity.
3. **Re-pair DualSense:** hold PS + Create until its light flashes rapidly, then press Pico BOOTSEL once to start discovery. PS alone may still target the old pairing.
4. Select Switch mode and start console pairing:

   ```powershell
   python tools/config/output_mode.py switch
   python tools/config/output_mode.py pair
   ```

5. Open **Controllers ? Change Grip/Order** on the console.

Keep the original donor controller off while using the Pico, because both use the same Bluetooth address. Do not delete that controller's registration from the console. Other console controller registrations do not need to be cleared.

Mode selection, learned wake settings and new pairings survive power cycles. Learning a different identity, disabling wake or clearing pairings can require re-pairing again.

## Normal use

Keep the Pico powered in **Switch Bluetooth mode**. Press PS to reconnect the DualSense. The firmware requests a wake burst after the controller reconnects, or on a PS-button edge when the console link is absent. Each burst lasts two seconds, with a five-second cooldown afterward. Advertising stops when the console link returns.

PC USB mode disables console connections and wake requests. This feature is separate from the PC USB `enable_wake` setting.

Double-click BOOTSEL to switch profiles. The mode LED repeats every two seconds:

| Pattern | Selected mode |
|---|---|
| One short pulse, long pause | PC USB |
| Two short pulses, long pause | Switch Bluetooth |

Pairing-clear, low-battery and discovery patterns take priority. The LED shows selection, not console connectivity. The LED-disable setting also disables the mode pattern.

## Diagnostics

```powershell
python tools/config/output_mode.py status
python tools/config/switch2_wake.py status
python tools/config/switch2_wake.py wake
```

| Field | Meaning |
|---|---|
| `result: saved` | Settings were written successfully |
| `active` | Valid settings were loaded at boot |
| `reboot_required` | Saved settings differ from the active boot identity |
| `learning` | Pico is listening for advertisements |
| `candidate_seen` | A valid candidate was detected during the last learning attempt; this alone does not confirm storage |
| `scan_reports` / `scan_matches` | Received BLE reports / matching wake beacons during the learning attempt |
| `hci_error` | Bluetooth controller command error; zero means no recorded error |
| `attempts` | Requested wake bursts, not successful console wakes |
| `advertising_requested` | Host-side advertisement request state, not a wake acknowledgment |

After applying settings and reconnecting power, expect `active: true` and `reboot_required: false`. Learning counters reset on reboot. `wake` requests a diagnostic burst in Switch mode; requests are canceled when the console is already connected. There is no console-wake success acknowledgment.

## Restore a backup or disable wake

```powershell
python tools/config/switch2_wake.py configure dist/switch2-wake.json
python tools/config/switch2_wake.py disable
```

Reconnect power after either command. Disabling restores the Pico's board-derived identity; re-pair DualSense and the console afterward. Settings use a separate CRC-protected record in the existing configuration flash page and preserve trigger settings.

Backup JSON uses display-order Bluetooth addresses: `source` is the registered donor controller and `target` is the console address from its wake beacon. Do not substitute a Wi-Fi MAC or a Switch 1 controller address. `product` is 8294 (`0x2066`, Joy-Con 2 L), 8295 (`0x2067`, Joy-Con 2 R) or 8297 (`0x2069`, Pro Controller 2).

The optional legacy PC capture method is still available; only that method needs PC Bluetooth and `bleak`:

```powershell
python -m pip install bleak
python tools/config/switch2_wake.py capture
python tools/config/switch2_wake.py configure dist/switch2-wake.json
```

## Troubleshooting and validation

- Learning requires PC mode and a registered controller waking a sleeping console. Standard pairing/reconnection advertisements are ignored.
- Public-address Joy-Con 2 L/R and Pro Controller 2 beacons are supported; random/private advertiser addresses are ignored.
- If DualSense fails to connect after identity activation, use PS + Create and restart discovery with BOOTSEL once.
- If the console fails to connect, select Switch mode, run `output_mode.py pair`, and open Change Grip/Order.
- Repeated power cycles normally do not require learning again. Console firmware changes and registration changes may affect compatibility.

Pico 2 W BLE reception, learning storage/activation and subsequent DualSense reconnection were observed locally. Automated tests cover parsing and failure handling. Game compatibility, long-term coexistence and reliable console sleep wake across systems remain unverified; see [validation](VALIDATION.md).

## Protocol references

- [ndeadly: Switch 2 Bluetooth interface](https://github.com/ndeadly/switch2_controller_research/blob/master/bluetooth_interface.md)
- [alexvnesta: wake packet research](https://github.com/alexvnesta/switch2controller/blob/master/docs/research/wake-packet.md)
- [tv: Switch 2 wake-up](https://github.com/tv/switch2-wake-up)

The implementation uses the documented packet format; those projects are not linked firmware dependencies.

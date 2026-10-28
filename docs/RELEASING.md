# Preparing a GitHub release

Current firmware version: `integrated-0.4.5-pico-learn`. Publish the exact source used to build the release assets. Local preparation does not create a tag, push commits or publish a release.

## Build and check

From the repository root on Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico_w
powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 -Board pico2_w -Test
git diff --check
Get-FileHash dist/Pico-W.uf2, dist/Pico-2-W.uf2 -Algorithm SHA256
```

Both firmware builds must pass the memory budget check. For this version the host suite has 13 C++ groups and 22 Python tests. Host tests do not establish hardware pairing, gameplay or wake compatibility.

Before uploading, confirm the intended source changes are included in the release commit and that version strings in `build.ps1` and `DEPENDENCIES.lock.json` agree. Exclude SDKs, compilers, build caches, logs, controller-address backups and local USB diagnostic reports from the source commit and public assets.

## Release assets

Attach the board-specific files with these exact names:

- `Pico-W.uf2`
- `Pico-2-W.uf2`
- `SHA256SUMS.txt` containing their SHA256 hashes

An optional `SwitchAndPCsense-0.4.5.zip` can bundle the binaries, README, wake guide, release notes, repository license, third-party notices and complete dependency license texts. Include the modified MPL-2.0 `firmware/src/audio.cpp` source in the bundle, and make the complete matching project source available with the release. Do not include `dist/switch2-wake.json` or raw device diagnostic logs in the archive.

Create a GitHub release from the matching source commit/tag only after reviewing the prepared assets. The repository's archived upstream workflow templates are not active release automation.

## Suggested release notes

### 0.4.5 ? Pico wake learning and selectable PC / Switch modes

- Switch between PC USB DualSense output and Switch Bluetooth output with BOOTSEL double-click; the mode is saved.
- Identify the mode with the onboard LED: one pulse for PC, two for Switch.
- Learn a registered Switch 2 controller's wake beacon directly on the Pico over a USB-controlled setup flow. PC Bluetooth is not required.
- Reject conflicting donors and preserve previous settings on timeout, cancellation or save failure.
- Provide matching builds for Pico W and Pico 2 W.

One-time wake learning changes the Pico's Bluetooth identity. Reconnect power and re-pair DualSense and the console afterward. Keep the original learned controller off while using the Pico.

Pico 2 W boot/CRC, mode persistence, BLE reception, wake-beacon learning and DualSense reconnection were observed locally. Console wake remains experimental; game-specific PC haptics/audio, latency and sustained Bluetooth stability need further hardware validation. Pico W excludes Opus speaker/microphone processing. Switch Bluetooth audio and NFC/Amiibo are not implemented.

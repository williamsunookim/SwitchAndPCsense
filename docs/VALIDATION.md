# Validation record

## Subsequent local setup — 2026-10-29

- A real registered controller wake beacon was learned: 33 matching beacons and
  `result: saved` were observed without HCI errors.
- After a power cycle, `active: true` and `reboot_required: false` confirmed that
  the learned identity was loaded.
- DualSense re-pairing through PS + Create and a new Pico discovery succeeded.
  Console connection was observed earlier in the setup; the user subsequently
  reported reconnection success. No quantitative gameplay or wake test was recorded.
- These observations do not establish reliable console sleep wake across systems.

## Pico wake-beacon learning — 2026-10-29

- Version: `integrated-0.4.5-pico-learn`; both board builds passed memory checks.
- 13 C++ host groups and 22 Python tests passed. Added bounded advertisement
  decoding, public-address/source filtering, timeout, cancellation, conflicting
  donor rejection, flash-failure rollback and USB learning-tool lifecycle checks.
- Connected Pico 2 W was flashed; version, 150 MHz operation and application
  CRC matched the new UF2 (`dist/PICO2_LEARN_FLASH_VERIFICATION.json`).
- Hardware scan with an impossible donor filter received 125 surrounding BLE
  reports in five seconds with HCI error 0. Cancellation and timeout both ended
  learning and retained existing settings; no real donor was stored. Restored
  the previously selected Switch Bluetooth mode afterward.
- Scan record: `dist/PICO2_LEARN_SCAN_VERIFICATION.json`.
- Capturing an actual paired Joy-Con 2 / Pro Controller 2 and waking a console
  remain unverified. The user must run `learn` and press the donor HOME button.

## Mode LED — 2026-10-29

- Version: `integrated-0.4.4-mode-led`; Pico W and Pico 2 W builds passed their
  memory budget checks. No new protocol or audio routing changes were made.
- Idle/connected mode indication repeats every two seconds: one 120 ms pulse
  for PC USB, two pulses for Switch Bluetooth. Pairing-clear, low-battery and
  pairing indications take priority; the existing LED-disable setting is honored.
- Connected Pico 2 W was flashed and its 150 MHz boot, version and application
  CRC were checked. Saved PC mode was retained. LED timing was not optically measured.
- Flash report: `dist/PICO2_LED_FLASH_VERIFICATION.json`.

## PC USB / Switch Bluetooth mode switching — 2026-10-29

- Version: `integrated-0.4.3-mode-switch`; both board builds passed memory checks.
- 13 C++ host groups and 19 Python tests passed. Mode tests cover toggling,
  persisted selection, save retry, blocking console wake in PC mode, excluding
  Switch rumble from PC, and retaining native PC trigger commands.
- Connected Pico 2 W was flashed; 150 MHz boot and application flash CRC matched
  `dist/Pico-2-W.uf2`.
- Hardware: selected Switch Bluetooth via USB, rebooted through BOOTSEL and
  reflashed the same image, confirmed saved Switch mode restored, then selected
  PC USB and confirmed current PC mode. No pairing information was cleared.
- DualSense was disconnected during the hardware mode check. PC gameplay,
  PC audio/haptics and actual console pairing/wake remain unverified.
- Opus encoder init/control remain in flash to preserve the 128 KiB heap reserve;
  encode entry points remain in RAM. PC audio timing needs hardware validation.
- Latest flash diagnostics: `dist/PICO2_MODE_FLASH_VERIFICATION.json`.

## Switch 2 wake experiment — 2026-10-28

- Version: `integrated-0.4.2-switch2-wake`.
- Both board Release builds linked and passed their memory budget checks.
- All 12 C++ host test groups and 19 Python tests passed, including wake packet,
  burst/cooldown/error handling, settings CRC/persistence and beacon decoding.
- Both UF2 files were checked for block magic, application family, numbering and
  flash bounds. The Pico 2 W image includes the SDK's RP2350 metadata block.
- Generated artifact sizes and SHA256 are in `dist/SWITCH2_WAKE_BUILD.json`.
- No board was flashed; console wake, BLE/Classic coexistence and reconnection
  after wake remain unverified on hardware. See [setup](SWITCH2_WAKE.md).

## Earlier structure validation

- Shared source: integrated-0.4.1-gyro-fix, Pico W and Pico 2 W.
- Both Release builds passed after consolidation.
- All 10 C++ host test groups and 14 Python unit tests passed.
- UF2 application blocks validated against each MCU family and flash range.
- 1,333 source/profile/library/license files matched the latest source SHA256 before removal of old copies.
- This reorganized build was not flashed during cleanup. Console pairing/gameplay were not tested.
- Local rollback archive: ../SwitchAndPCsense-dev-before-cleanup-20261028.zip

- External libraries moved to ignored `firmware/.deps`, pinned by commit in the dependency lock.
- Opus/WDL compared with upstream pinned sources: no local modifications (line endings normalized).
- Both builds and all host tests passed after the dependency move.
- Missing WDL cache auto-download was tested and the restored resampler SHA256 matched.
- Git upload candidates reduced from 1,401 to 149 files.

## Generated release files (ignored by Git)

- `Pico-W.uf2`: 970,240 bytes; SHA256 `9b7d67209e81318ec20ec58125b2f1a460ad5ebcb8f3560851a8d8d4ae151454`
- `Pico-2-W.uf2`: 1,579,008 bytes; SHA256 `de8627e6a9ec5c990c6340c6ed7f24a7d2d9dd432a58e91905fc4d2f7fce0faf`

## GitHub documentation and layout update

- Root README rewritten in English; current wireless behavior separated from upstream history.
- Upstream documents/scripts/metadata retained under docs/upstream.
- Host tools grouped under tools/config and tools/diagnostics; test imports updated.
- All host tests passed after the move; main documentation links and Python syntax checked.
- Firmware implementation and board build entry points were preserved.

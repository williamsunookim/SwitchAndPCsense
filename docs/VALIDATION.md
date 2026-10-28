# Structure validation ? 2026-10-28

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

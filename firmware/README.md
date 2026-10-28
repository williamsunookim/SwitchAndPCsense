# Firmware source

Shared firmware for Raspberry Pi Pico W and Pico 2 W.
See the [project README](../README.md) for current build and flashing instructions.

- `src/`: controller, Bluetooth, USB, motion and haptics implementation
- `boards/`, `cmake/`: board definitions and build helpers
- `profiles/`: trigger configuration examples
- `tests/`: portable host tests and hardware stubs
- `tools/`: build-time memory guard and Wireshark protocol helper
- `licenses/`: third-party license texts
- `.deps/`, `build/`: downloaded libraries and generated files (ignored by Git)

Host configuration and diagnostics are in [`tools/`](../tools/).
[Upstream documentation](../docs/upstream/README.md) describes an earlier project;
it is retained for reference and is not the current feature specification.

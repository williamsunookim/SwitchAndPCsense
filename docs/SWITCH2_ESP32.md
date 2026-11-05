# Switch 2 Pro Controller via ESP32-S3 (experimental)

> Status: code complete, **not yet tested on hardware**. Branch `feature/switch2-pro`.

The Switch 2 talks to its controllers over Bluetooth LE at a **5 ms connection
interval**, below the 7.5 ms minimum of the Bluetooth spec. The CYW43439 radio on
the Pico W / Pico 2 W cannot follow it: the console connects, sends nothing, and the
link drops (`0x28` instant passed / `0x08` supervision timeout). The ESP32-S3 can,
with ESP-IDF ≥ v6.1 (`CONFIG_BT_CTRL_BLE_MIN_CONN_INTERVAL_ENABLE`).

The ESP32-S3 has no Bluetooth Classic, so it cannot host the DualSense. The two
boards split the work:

```
DualSense ──BT Classic──▶ Pico 2 W ──UART 1 Mbaud──▶ ESP32-S3 ──BLE──▶ Switch 2
          ◀─haptics/PCM──          ◀──HD Rumble 2───          ◀─rumble─
```

| Board | Does |
|---|---|
| Pico W / Pico 2 W | DualSense connection, input parsing, HD Rumble → DualSense haptics, PC/Switch mode, LED, USB tools |
| ESP32-S3 | Pro Controller 2 advertising, GATT, Nintendo pairing (command `0x15`), LTK, input reports, wake |

## Parts

- Raspberry Pi Pico W or Pico 2 W (existing)
- **ESP32-S3-DevKitC-1-N8R8** (recommended) or Seeed Studio **XIAO ESP32S3**
- 4 jumper wires (female–female for header-to-header)

## Wiring

All signals are 3.3 V on both boards. No level shifter.

| Signal | Pico (GPIO / physical pin) | ESP32-S3-DevKitC-1 (header J1) |
|---|---|---|
| Pico TX → ESP RX | **GP4** / pin 6 | **GPIO18** / J1-11 |
| Pico RX ← ESP TX | **GP5** / pin 7 | **GPIO17** / J1-10 |
| Ground | **GND** / pin 8 (or 3, 13, 18…) | **GND** / J1-22 |
| 5 V supply (optional, see below) | **VBUS** / pin 40 | **5V** / J1-21 |

TX always goes to the other board's RX. Pico GP0/GP1 stay reserved for the debug
console.

```
   Pico 2 W                         ESP32-S3-DevKitC-1
 ┌──────────┐                      ┌──────────────────┐
 │ pin 6 GP4├──────────────────────┤GPIO18 (J1-11)    │
 │ pin 7 GP5├──────────────────────┤GPIO17 (J1-10)    │
 │ pin 8 GND├──────────────────────┤GND    (J1-22)    │
 │ pin40 VBUS├─ ─ ─ (optional) ─ ─ ┤5V     (J1-21)    │
 └────┬─────┘                      └──────────────────┘
      USB (power + PC tools)
```

**Power**

- *Development*: power each board from its own USB cable and connect only
  GP4, GP5 and GND. The ESP32 USB port gives flashing and the serial log.
- *Finished build*: one USB cable into the Pico; connect Pico VBUS (pin 40) to
  the ESP32 5V pin. Do **not** plug a USB cable into the ESP32 at the same time
  while the 5 V wire is connected.

**XIAO ESP32S3**: use D4 (GPIO5) as TX and D5 (GPIO6) as RX, and build with
`idf.py build -DLINK_TX_PIN=5 -DLINK_RX_PIN=6`. Its `5V` pin takes VBUS the
same way.

## Build and flash

### Pico

```powershell
./build.ps1 -Board pico2_w -Esp32Link   # or -Board pico_w
```

Output: `dist/Pico-2-W.uf2`; copy it to the board in BOOTSEL mode as usual. The
version string is `integrated-0.5.0-pro2-esp32`.

### ESP32-S3

Install ESP-IDF **v6.1 or newer** (older versions refuse the console's 5 ms
interval or stall the input stream), then:

```powershell
cd esp32
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

On boot the log shows the GATT handle map. `handles: in05=0x000a in09=0x000e
… combined=0x0016 … rsp2=0x001e` matches a real Pro Controller 2; a warning means
the map shifted (the console discovers by UUID, so it may still work).

## Use

1. Switch the Pico to Switch mode (BOOTSEL double click, LED blinks twice) or
   `python tools/config/output_mode.py switch`.
2. Start pairing: `python tools/config/output_mode.py pair`, or hold the ESP32
   **BOOT** button for 3 s.
3. On the Switch 2 open **Controllers → Change Grip/Order**.
4. The ESP32 log should show `connected: interval=15.00 ms`, then
   `encryption: status=0`, and a few seconds later
   `connection update … interval=5000 us`.
5. After pairing, the DualSense **PS** button sends the wake advertisement
   while the console sleeps.

The bond lives in ESP32 NVS. `pair` (or BOOT 3 s) forgets it.

## Diagnostics

```powershell
python tools/diagnostics/procon2_status.py
```

`esp32_link.alive: true` means the ESP32 is answering. `frames_received`
should keep rising; non-zero `crc_or_framing_errors` points at wiring or a
missing GND. The remaining fields come from the ESP32 (`bonded`, `encrypted`,
`input_subscribed`, `connection_interval_us`, `rumble_packets`).

## UART protocol

Defined in `firmware/src/switch2_link.h`, shared by both firmwares and covered by
`firmware/tests/test_switch2_link.cpp`.

Frame: `A5 5A <type> <len> <payload> <crc16-lo> <crc16-hi>` (CRC-16/CCITT-FALSE
over type, len, payload), 1 000 000 baud 8N1.

| Type | Direction | Payload |
|---|---|---|
| `0x01` CONTROL | Pico → ESP | protocol version, enabled flag; every 100 ms |
| `0x02` STATE | Pico → ESP | packed `ControllerState` (33 B) |
| `0x03` PAIR | Pico → ESP | — forget bond, advertise for a new console |
| `0x04` WAKE | Pico → ESP | — wake advertisement for 3 s |
| `0x81` STATUS | ESP → Pico | flags, counters, addresses (45 B); every 100 ms |
| `0x82` RUMBLE | ESP → Pico | raw 33-byte HD Rumble 2 write |
| `0x83` LEDS | ESP → Pico | player LED pattern |

## Not verified yet

- Anything on real hardware: the ESP32 firmware has not been compiled yet
  (no ESP-IDF on the development PC); the Pico side builds for both boards.
- Report `0x09` motion packing is undocumented; motion is only sent in report
  `0x05` when the console enables it.
- HD Rumble 2 → DualSense uses the nominal-Hz approximation in
  `switch2_approximate_rumble`, not a measured transfer function.

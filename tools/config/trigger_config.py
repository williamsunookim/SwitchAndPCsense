"""Configure this bridge in PC mode. Requires optional `pip install hidapi`."""
import argparse
import json
import struct
import time
from pathlib import Path

def encode_side(s):
    mode = s.get('mode', 0)
    if isinstance(mode, str):
        mode = ['off', 'resistance', 'bow', 'recoil', 'rumble'].index(mode)
    values = [mode, s.get('start', 2), s.get('end', 9)] + s.get('curve', [0]*10)
    values += [s.get('kick_strength', 0), s.get('kick_ms', 60), s.get('rumble_strength', 0), s.get('frequency', 35)]
    if len(values) != 17 or any(type(v) is not int or not 0 <= v <= 255 for v in values):
        raise ValueError('Each curve must have ten integer forces and all fields must be bytes')
    if not (0 <= values[0] <= 4 and 0 <= values[1] <= values[2] <= 9 and
            all(0 <= v <= 8 for v in values[3:13]) and values[13] <= 8 and
            values[14] <= 200 and values[15] <= 8 and values[16] > 0):
        raise ValueError('Invalid trigger mode, zone, force, duration or frequency')
    return bytes(values)

def encode_settings(data):
    profile = data.get('profile', 2)
    if profile not in (0, 1, 2, 3) or data.get('force', 0) not in (0, 1):
        raise ValueError('Invalid profile/force')
    return bytes([1, profile, int(data.get('force', 0))]) + encode_side(data.get('left', {})) + encode_side(data.get('right', {}))

def decode_settings(raw):
    if len(raw) < 37 or raw[0] != 1:
        raise ValueError('Invalid bridge settings response')
    def side(b):
        return dict(mode=b[0], start=b[1], end=b[2], curve=list(b[3:13]),
                    kick_strength=b[13], kick_ms=b[14], rumble_strength=b[15], frequency=b[16])
    return dict(version=raw[0], profile=raw[1], force=raw[2], left=side(raw[3:20]), right=side(raw[20:37]))

def preset(profile):
    s = {'profile': 3 if profile == 'off' else (0 if profile == 'auto' else 1), 'force': 0}
    if profile == 'botw':
        s['right'] = {'mode': 'bow', 'end': 9, 'curve': [0,0,1,2,2,3,4,5,6,6]}
    return s

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile', choices=['off','auto','botw'])
    ap.add_argument('--settings', type=Path, help='custom JSON settings')
    ap.add_argument('--force', action='store_true', help='Explicitly override PC native triggers')
    ap.add_argument('--validate', action='store_true', help='Validate/encode without connecting hardware')
    args = ap.parse_args()
    if args.force and not (args.settings or args.profile):
        ap.error('--force requires --profile or --settings')
    if args.settings and args.profile:
        ap.error('Choose --settings or --profile')
    data = json.loads(args.settings.read_text(encoding='utf-8')) if args.settings else preset(args.profile or 'off')
    if args.force: data['force'] = 1
    payload = encode_settings(data)
    if args.validate:
        print(json.dumps(decode_settings(payload), indent=2)); return
    import hid
    device = None
    for item in hid.enumerate(0x054c, 0):
        if item['product_id'] not in (0x0ce6,0x0df2) or item.get('usage_page') != 1:
            continue
        candidate = hid.device()
        try:
            candidate.open_path(item['path'])
            version = bytes(candidate.get_feature_report(0xf8,64))[1:]
            if b'integrated-' in version:
                device = candidate; break
        except OSError:
            pass
        candidate.close()
    if device is None:
        raise SystemExit('Integrated bridge not found. Double-click BOOTSEL for PC mode and pair DualSense.')
    try:
        if args.profile or args.settings:
            report = bytes([0xf6,0x20]) + payload
            device.send_feature_report(report.ljust(64,b'\0'))
            time.sleep(1.5) # deferred persistence
        actual = bytes(device.get_feature_report(0xf6,64))
        current = decode_settings(actual[1:])
        if (args.profile or args.settings) and actual[1:38] != payload:
            raise SystemExit('Settings readback mismatch')
        print(json.dumps(current, indent=2))
        diagnostic = bytes(device.get_feature_report(0xf9,64))[1:]
        if len(diagnostic) >= 14 and diagnostic[2] == 1:
            print('Diagnostics:', dict(profile=diagnostic[3], flags=diagnostic[4], force=diagnostic[5],
                  safety_stops=struct.unpack_from('<I',diagnostic,6)[0],
                  input_age_ms=struct.unpack_from('<I',diagnostic,10)[0]))
    finally:
        device.close()

if __name__ == '__main__':
    main()

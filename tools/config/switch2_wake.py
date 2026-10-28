"""Capture a paired Switch 2 controller's wake beacon and configure the Pico."""
import argparse
import asyncio
import json
from pathlib import Path
import re
import struct
import time


def mac(value):
    if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', value):
        raise ValueError('Expected Bluetooth MAC AA:BB:CC:DD:EE:FF')
    return bytes.fromhex(value.replace(':', ''))


def display(value):
    return ':'.join(f'{b:02X}' for b in value)


def decode_beacon(address, manufacturer):
    # Bleak strips the 2-byte company ID from manufacturer_data values.
    data = manufacturer.get(0x0553, b'')
    if len(data) != 24 or data[:5] != bytes.fromhex('0100037e05'):
        return None
    product = int.from_bytes(data[5:7], 'little')
    if product not in (0x2066, 0x2067, 0x2069) or data[7:10] != b'\x00\x01\x81':
        return None
    if data[16:] != b'\x0f' + bytes(7):
        return None
    return {'source': display(mac(address)), 'target': display(data[10:16][::-1]),
            'product': product}


async def capture(args):
    from bleak import BleakScanner
    found = {}

    def detected(device, advertisement):
        if args.source and device.address.upper() != args.source.upper():
            return
        profile = decode_beacon(device.address, advertisement.manufacturer_data)
        if profile:
            found[device.address] = profile

    print('Put Switch 2 to sleep, then press HOME on its paired Joy-Con 2 / Pro Controller 2.')
    async with BleakScanner(detection_callback=detected):
        await asyncio.sleep(args.seconds)
    if not found:
        raise RuntimeError('No wake beacon captured. Retry with the controller and PC closer together.')
    if len(found) != 1:
        raise RuntimeError('Multiple controllers captured. Retry with --source MAC: ' + ', '.join(found))
    profile = next(iter(found.values()))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(profile, indent=2) + '\n', encoding='utf-8')
    print(f'Saved {args.out}: {profile}')
    print('Turn the original controller OFF before enabling this profile on the Pico.')


def open_pico(path=None):
    import hid
    matches = []
    for item in hid.enumerate(0x054c, 0x0ce6):
        if path and item['path'].decode(errors='replace') != path:
            continue
        device = hid.device()
        try:
            device.open_path(item['path'])
            version = bytes(device.get_feature_report(0xf8, 64))[1:].split(b'\0', 1)[0]
            if not version.startswith(b'integrated-'):
                device.close()
                continue
            device.send_feature_report(bytes([0xf6, 0x30, 5]) + bytes(61))
            page = bytes(device.get_feature_report(0xf9, 64))[1:]
            if page[:3] != b'S2\x01':
                device.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
                device.close()
                continue
            matches.append(device)
        except OSError:
            device.close()
    if len(matches) != 1:
        for device in matches:
            device.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
            device.close()
        raise RuntimeError('Connect exactly one Pico with Switch 2 wake firmware, or use --path.')
    return matches[0]


def status(device):
    page = bytes(device.get_feature_report(0xf9, 64))[1:]
    if len(page) < 24 or page[:3] != b'S2\x01':
        raise RuntimeError('Wake diagnostics not supported')
    result = ('idle', 'queued', 'burst ended (console wake not confirmed)', 'busy/cooldown',
              'not configured/ready', 'saved', 'flash save failed', 'BLE command failed', 'console already connected', 'PC mode: wake disabled',
              'learning', 'learning timed out: no wake beacon', 'multiple wake beacons: settings unchanged', 'learning canceled',
              'reserved', 'select PC mode before learning')
    return {'active': bool(page[3] & 1), 'advertising_requested': bool(page[3] & 2),
            'reboot_required': bool(page[3] & 4),
            'learning': bool(page[3] & 8), 'candidate_seen': bool(page[3] & 16),
            'learning_supported': bool(page[3] & 32),
            'scan_reports': int.from_bytes(page[24:28], 'little') if len(page) >= 32 else None,
            'scan_matches': int.from_bytes(page[28:32], 'little') if len(page) >= 32 else None,
            'result': result[page[4]] if page[4] < len(result) else page[4],
            'hci_error': page[5], 'attempts': int.from_bytes(page[6:10], 'little'),
            'source': display(page[10:16]), 'target': display(page[16:22]),
            'product': int.from_bytes(page[22:24], 'little')}


def learn(device, args):
    initial = status(device)
    if not initial['learning_supported']:
        raise RuntimeError('Pico learning requires integrated-0.4.5 firmware or newer')
    if initial['learning']:
        raise RuntimeError('Learning is already active; wait or use cancel-learn')
    source = mac(args.source) if args.source else bytes(6)
    device.send_feature_report(bytes([0xf6, 0x34, args.seconds]) + source + bytes(55))
    answer = status(device)
    if not answer['learning']:
        raise RuntimeError(f"Learning did not start: {answer['result']}")
    print(f'Pico is listening for {args.seconds} seconds. PC Bluetooth is not used.', flush=True)
    print('Put Switch 2 to sleep, then wake it with HOME on ONE paired Joy-Con 2 / Pro Controller 2.', flush=True)
    seen = False
    try:
        deadline = time.monotonic() + args.seconds + 5
        while answer['learning']:
            if time.monotonic() >= deadline:
                raise RuntimeError('Pico learning did not finish within the expected time')
            time.sleep(0.25)
            answer = status(device)
            if answer['candidate_seen'] and not seen:
                print('Wake beacon detected. Checking for conflicting controllers until the window ends...', flush=True)
                seen = True
        if answer['result'] != 'saved':
            raise RuntimeError(f"Learning failed: {answer['result']} (HCI error {answer['hci_error']}). Previous settings retained.")
        profile = {key: answer[key] for key in ('source', 'target', 'product')}
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(profile, indent=2) + '\n', encoding='utf-8')
        print(f'Saved on Pico; backup written to {args.out}. No configure command is needed.')
        print('Turn the original controller OFF, then unplug/replug Pico and re-pair DualSense and Switch.')
    finally:
        if answer['learning']:
            device.send_feature_report(bytes([0xf6, 0x35]) + bytes(62))


def configure(device, profile):
    source, target = mac(profile['source']), mac(profile['target'])
    product = profile['product']
    if isinstance(product, str):
        product = int(product, 0)
    if product not in (0x2066, 0x2067, 0x2069):
        raise ValueError('Product must be 0x2066, 0x2067 or 0x2069')
    if source in (bytes(6), b'\xff'*6) or target in (bytes(6), b'\xff'*6) or source == target:
        raise ValueError('Invalid source or target address')
    device.send_feature_report(bytes([0xf6, 0x31]) + source + target + struct.pack('<H', product) + bytes(48))
    answer = status(device)
    if answer['result'] != 'saved' or answer['source'] != display(source) or answer['target'] != display(target) or answer['product'] != product:
        raise RuntimeError(f'Configuration was not saved: {answer}')
    print('Saved. Unplug/replug the Pico, then re-pair DualSense and the wireless Switch connection.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--path', help='Select one HID path when multiple Picos are connected')
    sub = parser.add_subparsers(dest='command', required=True)
    scan = sub.add_parser('capture')
    scan.add_argument('--source', help='Only capture this controller MAC')
    scan.add_argument('--seconds', type=float, default=20)
    scan.add_argument('--out', type=Path, default=Path('dist/switch2-wake.json'))
    learning = sub.add_parser('learn', help='Learn directly on Pico via USB; no PC Bluetooth required')
    learning.add_argument('--source', help='Only learn this controller MAC (optional)')
    learning.add_argument('--seconds', type=int, choices=range(5, 121), default=30, metavar='5..120')
    learning.add_argument('--out', type=Path, default=Path('dist/switch2-wake.json'))
    setup = sub.add_parser('configure')
    setup.add_argument('profile', type=Path)
    for name in ('status', 'wake', 'disable', 'cancel-learn'):
        sub.add_parser(name)
    args = parser.parse_args()
    if args.command == 'capture':
        asyncio.run(capture(args))
        return
    device = open_pico(args.path)
    try:
        if args.command == 'configure':
            configure(device, json.loads(args.profile.read_text(encoding='utf-8')))
        elif args.command == 'learn':
            learn(device, args)
        elif args.command == 'cancel-learn':
            device.send_feature_report(bytes([0xf6, 0x35]) + bytes(62))
        elif args.command == 'disable':
            device.send_feature_report(bytes([0xf6, 0x31]) + bytes(62))
            if status(device)['result'] != 'saved':
                raise RuntimeError('Disable failed; wait for the burst/cooldown and retry')
            print('Disabled. Unplug/replug and re-pair to restore the original Pico identity.')
        elif args.command == 'wake':
            device.send_feature_report(bytes([0xf6, 0x32]) + bytes(62))
        print(json.dumps(status(device), indent=2))
    finally:
        device.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
        device.close()


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, ValueError, KeyError, OSError, ImportError) as exc:
        raise SystemExit(str(exc))

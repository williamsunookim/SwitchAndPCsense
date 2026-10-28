"""Show or select PC USB / Switch Bluetooth output on the connected Pico."""
import argparse
import json
import time
from switch2_wake import open_pico


def read_mode(device):
    device.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
    raw = bytes(device.get_feature_report(0xf9, 64))[1:]
    if len(raw) < 62 or raw[42:45] != b'WL\x01':
        raise RuntimeError('Output mode diagnostics unavailable')
    version = bytes(device.get_feature_report(0xf8, 64))[1:].split(b'\0', 1)[0].decode()
    if not version.startswith(('integrated-0.4.3-', 'integrated-0.4.4-', 'integrated-0.4.5-')):
        raise RuntimeError('Requires integrated-0.4.3 mode-switch firmware')
    return dict(mode='switch' if raw[45] & 8 else 'pc',
                dualsense_connected=bool(raw[17] & 2),
                switch_connected=bool(raw[45] & 4), version=version)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('status', 'pc', 'switch', 'pair'), nargs='?', default='status')
    args = parser.parse_args()
    device = open_pico()
    try:
        current = read_mode(device)
        if args.mode == 'pair':
            if current['mode'] != 'switch':
                raise RuntimeError('Select switch mode before pairing')
            device.send_feature_report(bytes([0xf6, 0x21]) + bytes(62))
            print('Switch pairing started. Open Controllers > Change Grip/Order on the console.')
        elif args.mode != 'status' and current['mode'] != args.mode:
            try:
                device.send_feature_report(bytes([0xf6, 0x33, int(args.mode == 'switch')]) + bytes(61))
            except OSError:
                pass  # Mode selection re-enumerates USB; verify after reconnect.
            device.close()
            device = None
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                time.sleep(0.5)
                try:
                    device = open_pico()
                    current = read_mode(device)
                    if current['mode'] != args.mode:
                        raise RuntimeError('Mode did not change')
                    break
                except (OSError, RuntimeError):
                    if device:
                        device.close()
                        device = None
            else:
                raise RuntimeError('Mode change could not be verified; reconnect the Pico and check status')
        print(json.dumps(read_mode(device), indent=2))
    finally:
        if device:
            device.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
            device.close()


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ImportError) as exc:
        raise SystemExit(str(exc))

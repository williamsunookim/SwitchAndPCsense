"""Capture the IMU samples actually sent to the Switch and compare gyro-integrated
pitch/roll with accelerometer-derived pitch/roll.

Usage: python capture_motion.py SECONDS OUT.csv
"""
import csv, math, struct, sys, time
import hid

SWITCH_GYRO_COUNTS_PER_DPS = 14.284
STEP_S = 0.005


def open_pico():
    for item in hid.enumerate(0x054c, 0x0ce6):
        d = hid.device()
        try:
            d.open_path(item['path'])
            v = bytes(d.get_feature_report(0xf8, 64))[1:].split(b'\0', 1)[0]
            if v.startswith(b'integrated-0.4'):
                return d
        except OSError:
            pass
        d.close()
    raise SystemExit('Pico not found')


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20
    path = sys.argv[2] if len(sys.argv) > 2 else 'motion_capture.csv'
    d = open_pico()
    d.send_feature_report(bytes([0xf6, 0x30, 1]) + bytes(61))
    rows, last, missed = [], None, 0
    end = time.monotonic() + seconds
    try:
        while time.monotonic() < end:
            r = bytes(d.get_feature_report(0xf9, 64))[1:]
            if r[0:1] != b'M' or not r[2]:
                continue
            n = struct.unpack_from('<I', r, 11)[0]
            if n == last:
                continue
            if last is not None and n != last + 1:
                missed += n - last - 1
            last = n
            t = time.monotonic()
            # Report holds newest first; store oldest first.
            for i in (2, 1, 0):
                rows.append((n, t) + struct.unpack_from('<6h', r, 19 + i * 12))
    finally:
        d.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
        d.close()
    with open(path, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['report', 'host_t', 'ax', 'ay', 'az', 'gx', 'gy', 'gz'])
        w.writerows(rows)
    print(f'captured {len(rows)//3} reports, missed {missed}, saved {path}')


if __name__ == '__main__':
    main()

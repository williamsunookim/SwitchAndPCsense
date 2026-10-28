"""Read the Switch motion diagnostic page (F6 0x30 + F9) from the Pico over PC USB."""
import struct, sys, time
import hid


def open_pico():
    for item in hid.enumerate(0x054c, 0x0ce6):
        d = hid.device()
        try:
            d.open_path(item['path'])
            v = bytes(d.get_feature_report(0xf8, 64))[1:].split(b'\0', 1)[0]
            if v.startswith(b'integrated-0.4'):
                return d, v.decode()
        except OSError:
            pass
        d.close()
    raise SystemExit('Pico not found')


def read_page(d):
    d.send_feature_report(bytes([0xf6, 0x30, 1]) + bytes(61))
    try:
        r = bytes(d.get_feature_report(0xf9, 64))[1:]
    finally:
        d.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
    if r[0:1] != b'M':
        raise SystemExit('motion page not supported: ' + r[:8].hex())
    stale, reports, gap = struct.unpack_from('<3I', r, 7)
    samples = [struct.unpack_from('<6h', r, 19 + i * 12) for i in range(3)]
    return dict(imu_mode=r[1], imu_enabled=r[2], sensitivity=r[3:7].hex(),
                stale_samples=stale, input_reports=reports, max_gap_us=gap, samples=samples,
                watchdog_boot=r[55], stuck_stage=r[56], watchdog_resets=r[57] | r[58] << 8)


if __name__ == '__main__':
    d, version = open_pico()
    print('firmware:', version)
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    for _ in range(count):
        p = read_page(d)
        print(f"mode=0x{p['imu_mode']:02x} en={p['imu_enabled']} sens={p['sensitivity']} "
              f"stale={p['stale_samples']} reports={p['input_reports']} maxgap={p['max_gap_us']}us")
        print(f"   last boot by watchdog={p['watchdog_boot']} stuck_stage={p['stuck_stage']} watchdog_resets={p['watchdog_resets']}")
        for s in p['samples']:
            print('   accel(x,y,z)=%6d %6d %6d  gyro(x,y,z)=%6d %6d %6d' % s)
        time.sleep(0.5)
    d.close()

"""Read the DualSense sensor-clock spacing histogram (page 4) from the Pico."""
import struct
import hid


def main():
    for item in hid.enumerate(0x054c, 0x0ce6):
        d = hid.device()
        try:
            d.open_path(item['path'])
            v = bytes(d.get_feature_report(0xf8, 64))[1:].split(b'\0', 1)[0]
            if not v.startswith(b'integrated-0.4'):
                continue
            d.send_feature_report(bytes([0xf6, 0x30, 4]) + bytes(61))
            try:
                r = bytes(d.get_feature_report(0xf9, 64))[1:]
            finally:
                d.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
            if r[0:1] != b'G':
                raise SystemExit('gap page not supported')
            b = struct.unpack_from('<6I', r, 1)
            total = sum(b[:5]) or 1
            for name, n in zip(('<=5ms', '5-10ms', '10-20ms', '20-50ms', '>50ms'), b[:5]):
                print(f'{name:8s} {n:7d}  {100 * n / total:5.1f}%')
            print('repeated timestamps', b[5], ' total intervals', sum(b[:5]))
            return
        except OSError:
            pass
        finally:
            d.close()
    raise SystemExit('Pico not found')


if __name__ == '__main__':
    main()

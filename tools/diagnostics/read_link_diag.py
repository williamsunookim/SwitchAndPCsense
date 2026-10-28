"""Read wireless link diagnostics from the Pico over PC USB: uptime, Switch link, 14-event history."""
import struct, sys
import hid

EVENTS = {0x03: 'CONN_COMPLETE', 0x05: 'DISCONNECT', 0x06: 'AUTH_COMPLETE', 0x08: 'ENCRYPT_CHANGE',
          0x17: 'LINK_KEY_REQ', 0x31: 'IO_CAP_RESP', 0x36: 'SSP_COMPLETE', 0x16: 'PIN_REQ',
          0x71: 'L2CAP_OPEN', 0xff: 'CMD_STATUS_ERR',
          0x80: 'LOCAL: re-pair request', 0x81: 'LOCAL: auth failed, key dropped',
          0x82: 'LOCAL: host HID unplug', 0x83: 'LOCAL: L2CAP open failed',
          0x84: 'LOCAL: L2CAP channel closed', 0x85: 'LOCAL: no HID 20s after ACL',
          0x86: 'ignored stale L2CAP request fail', 0x32: 'IO_CAP_REQ'}


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


def page(d, n):
    d.send_feature_report(bytes([0xf6, 0x30, n]) + bytes(61))
    try:
        return bytes(d.get_feature_report(0xf9, 64))[1:]
    finally:
        d.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))


if __name__ == '__main__':
    d, version = open_pico()
    print('firmware:', version)
    f9 = bytes(d.get_feature_report(0xf9, 64))[1:]
    uptime = struct.unpack_from('<I', f9, 30)[0]
    wl = f9[42:62]
    reports, commands = struct.unpack_from('<2I', wl, 9)
    m = page(d, 1)
    print(f"uptime={uptime/1000:.1f}s switch_link={'up' if wl[3] & 4 else 'DOWN'} last_err=0x{wl[4]:02x} "
          f"reports={reports} commands={commands} watchdog_resets={m[57] | m[58] << 8}")
    events = []
    for n in (2, 3):
        r = page(d, n)
        if r[0:1] != b'E':
            raise SystemExit('event page not supported')
        for i in range(7):
            e = r[2 + i * 8: 10 + i * 8]
            if len(events) < r[1] and any(e):
                events.append(e)
    for e in reversed(events):
        link = e[1] | e[2] << 8
        t = struct.unpack_from('<I', e, 4)[0]
        print(f"  {t/1000:9.3f}s  {EVENTS.get(e[0], hex(e[0])):34s} link=0x{link:04x} status/reason=0x{e[3]:02x}")
    d.close()

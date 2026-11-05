"""Read native Switch 2 Pro Controller BLE diagnostics over the Pico USB port."""
import json
import struct
from verify_board import read_device


def main():
    device, base = read_device()
    if device is None:
        raise SystemExit('Current Pico firmware not found on USB')
    try:
        device.send_feature_report(bytes([0xf6, 0x30, 6]) + bytes(61))
        page = bytes(device.get_feature_report(0xf9, 64))[1:]
        if len(page) < 55 or page[:3] != b'P2\x01':
            raise SystemExit('Native Pro Controller 2 diagnostics unavailable')
        flags = page[3]
        values = struct.unpack_from('<6I', page, 8)
        report = dict(version=base['version'], stack_ready=bool(flags & 1),
                      bonded=bool(flags & 2), encrypted=bool(flags & 4),
                      input_subscribed=bool(flags & 8), pairing=bool(flags & 16),
                      crypto_and_gatt_valid=bool(flags & 32), advertising_requested=bool(flags & 64),
                      bond_save_pending=bool(flags & 128), last_error=page[4],
                      pairing_stage=page[5], selected_report=page[6], features=page[7],
                      connection_interval_us=values[0], input_reports=values[1],
                      commands=values[2], rumble_packets=values[3],
                      unsupported_rumble=values[4], rejected_commands=values[5],
                      local_address=':'.join(f'{b:02X}' for b in page[32:38]),
                      peer_address=':'.join(f'{b:02X}' for b in page[38:44]),
                      last_command=page[44], last_subcommand=page[45],
                      att_mtu=int.from_bytes(page[46:48], 'little'),
                      wake_attempts=int.from_bytes(page[48:52], 'little'), pending_replies=page[52],
                      att_received=page[53], att_sent=page[54])
        device.send_feature_report(bytes([0xf6, 0x30, 7]) + bytes(61))
        trace=bytes(device.get_feature_report(0xf9,64))[1:]
        if trace[:3]==b'AT\x01':
            report['att_history']=[dict(incoming=bool(trace[4+i*8]),opcode=trace[5+i*8],
                first_field=int.from_bytes(trace[6+i*8:8+i*8],'little'),
                bytes=int.from_bytes(trace[8+i*8:10+i*8],'little'),
                l2cap_cid=int.from_bytes(trace[10+i*8:12+i*8],'little')) for i in range(min(trace[3],6))]
        print(json.dumps(report, indent=2))
    finally:
        device.send_feature_report(bytes([0xf6, 0x30, 0]) + bytes(61))
        device.close()


if __name__ == '__main__':
    main()

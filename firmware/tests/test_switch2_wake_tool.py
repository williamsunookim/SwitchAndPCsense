import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('wake_tool', Path(__file__).resolve().parents[2] / 'tools/config/switch2_wake.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class WakeToolTests(unittest.TestCase):
    def test_capture_known_wake(self):
        # Published on-air example, manufacturer company ID removed by Bleak.
        data = bytes.fromhex('0100037e056620000181971f9955e2980f00000000000000')
        profile = tool.decode_beacon('98:e2:55:ae:f7:d8', {0x0553: data})
        self.assertEqual(profile, {'source': '98:E2:55:AE:F7:D8', 'target': '98:E2:55:99:1F:97', 'product': 0x2066})

    def test_ignore_pairing_and_other_manufacturers(self):
        data = bytes.fromhex('0100037e056620000100971f9955e2980f00000000000000')
        self.assertIsNone(tool.decode_beacon('98:E2:55:AE:F7:D8', {0x0553: data}))
        self.assertIsNone(tool.decode_beacon('98:E2:55:AE:F7:D8', {0x004c: data}))
        self.assertIsNone(tool.decode_beacon('98:E2:55:AE:F7:D8', {0x0553: data[:-1]}))

    def test_reject_bad_address(self):
        for value in ('', '98-E2-55-AE-F7-D8', '98:E2:55:AE:F7', 'GG:E2:55:AE:F7:D8'):
            with self.assertRaises(ValueError):
                tool.mac(value)

    def test_configure_rejects_before_usb_write(self):
        class NoWrites:
            def send_feature_report(self, _):
                raise AssertionError('Invalid profile reached device')
        with self.assertRaises(ValueError):
            tool.configure(NoWrites(), {'source': '00:00:00:00:00:00', 'target': '98:E2:55:99:1F:97', 'product': 0x2066})
        with self.assertRaises(ValueError):
            tool.configure(NoWrites(), {'source': '98:E2:55:AE:F7:D8', 'target': '98:E2:55:99:1F:97', 'product': 0x2009})

    def test_status_does_not_claim_wake_success(self):
        class Device:
            def get_feature_report(self, *_):
                return bytes([0xf9]) + b'S2\x01\x01\x02\x00' + bytes(18)
        state = tool.status(Device())
        self.assertIn('not confirmed', state['result'])
        self.assertEqual(state['attempts'], 0)


if __name__ == '__main__':
    unittest.main()

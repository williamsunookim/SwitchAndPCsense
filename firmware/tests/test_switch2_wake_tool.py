import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from tempfile import TemporaryDirectory

spec = importlib.util.spec_from_file_location('wake_tool', Path(__file__).resolve().parents[2] / 'tools/config/switch2_wake.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class WakeToolTests(unittest.TestCase):
    @staticmethod
    def device(pages):
        class Device:
            def __init__(self):
                self.pages=iter(pages)
                self.sent=[]
            def get_feature_report(self, *_):
                flags,result=next(self.pages)
                return bytes([0xf9])+b'S2\x01'+bytes([flags,result,0])+bytes(4)+bytes.fromhex('98e255aef7d898e255991f976620')
            def send_feature_report(self, data):
                self.sent.append(data)
        return Device()

    def test_pico_learning_saves_without_configure(self):
        device=self.device([(32,0),(40,10),(48,5)])
        with TemporaryDirectory() as directory, patch.object(tool.time,'sleep'), patch.object(tool.time,'monotonic',return_value=0):
            path=Path(directory)/'wake.json'
            tool.learn(device,SimpleNamespace(source=None,seconds=5,out=path))
            self.assertIn('98:E2:55:99:1F:97',path.read_text())
        self.assertEqual(len(device.sent),1)
        self.assertEqual(len(device.sent[0]),64)
        self.assertEqual(device.sent[0][:3],bytes([0xf6,0x34,5]))

    def test_pico_timeout_preserves_backup(self):
        device=self.device([(32,0),(40,10),(32,11)])
        with TemporaryDirectory() as directory, patch.object(tool.time,'sleep'), patch.object(tool.time,'monotonic',return_value=0):
            path=Path(directory)/'wake.json';path.write_text('original')
            with self.assertRaisesRegex(RuntimeError,'timed out'):
                tool.learn(device,SimpleNamespace(source=None,seconds=5,out=path))
            self.assertEqual(path.read_text(),'original')

    def test_pico_learning_host_timeout_cancels_scan(self):
        device=self.device([(32,0),(40,10)])
        with patch.object(tool.time,'monotonic',side_effect=[0,11]):
            with self.assertRaisesRegex(RuntimeError,'expected time'):
                tool.learn(device,SimpleNamespace(source=None,seconds=5,out=Path('unused.json')))
        self.assertEqual(device.sent[-1][:2],bytes([0xf6,0x35]))

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

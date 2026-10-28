import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'config'))
from trigger_config import encode_settings, decode_settings, preset
class TriggerConfigTests(unittest.TestCase):
 def test_wire_preset(self):
  wire=encode_settings(preset('botw'))
  self.assertEqual(len(wire),37)
  self.assertEqual(wire[:3],bytes([1,1,0]))
  self.assertEqual(wire[20:23],bytes([2,2,9]))
  self.assertEqual(decode_settings(wire)['right']['curve'],[0,0,1,2,2,3,4,5,6,6])
 def test_off(self): self.assertEqual(encode_settings(preset('off'))[1],3)
 def test_bad_zone(self):
  with self.assertRaises(ValueError): encode_settings({'right':{'start':9,'end':2}})
 def test_bad_curve(self):
  for curve in ([0]*9,[9]*10):
   with self.assertRaises(ValueError): encode_settings({'right':{'curve':curve}})
 def test_bad_duration(self):
  with self.assertRaises(ValueError): encode_settings({'left':{'kick_ms':201}})
 def test_zero_frequency(self):
  with self.assertRaises(ValueError): encode_settings({'left':{'frequency':0}})
 def test_bad_response(self):
  with self.assertRaises(ValueError): decode_settings(bytes(37))
if __name__=='__main__': unittest.main()

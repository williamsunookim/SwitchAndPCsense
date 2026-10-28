"""Read the wireless build's independent DualSense/Switch status via USB HID."""
import argparse,json,time
from verify_board import read_device
from pathlib import Path

def read_status():
 d,base=read_device()
 if d is None:raise SystemExit('Wireless Pico W not found on USB')
 try:
  raw=bytes(d.get_feature_report(0xf9,64))[1:]
  assert raw[42:45]==b'WL\x01','Wireless diagnostic signature missing'
  flags=raw[45]
  base.update(bluetooth_stack_ready=bool(flags&1),saved_switch_host=bool(flags&2),
   output_mode='switch_bluetooth' if flags&8 else 'pc_usb',
   switch_connected=bool(flags&4),last_switch_error=raw[46],
   switch_acl_handle=int.from_bytes(raw[47:49],'little'),
   switch_control_cid=raw[49],switch_interrupt_cid=raw[50],
   switch_reports_sent=int.from_bytes(raw[51:55],'little'),
   switch_commands_received=int.from_bytes(raw[55:59],'little'),
   last_peer_transaction=raw[59],last_peer_report_id=raw[60],peer_matches_dualsense=bool(raw[61]),
   effects_profile=raw[3],effects_live=bool(raw[4]&1))
  config=bytes(d.get_feature_report(0xf6,64))[1:]
  if len(config)>=63 and config[37]==ord('T'):
   base['local_bluetooth_address']=':'.join(f'{b:02x}' for b in config[57:63])
   trace=config[39:57];base['connection_events']=[dict(event=trace[i*6],handle=int.from_bytes(trace[i*6+1:i*6+3],'little'),status=trace[i*6+3],time_ms_mod65536=int.from_bytes(trace[i*6+4:i*6+6],'little')) for i in range(config[38])]
  return base
 finally:d.close()

if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path);args=ap.parse_args()
 report=read_status();print(json.dumps(report,indent=2))
 if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n')

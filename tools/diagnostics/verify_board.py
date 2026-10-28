"""Read only this build's HID diagnostics and verify its running flash CRC."""
from pathlib import Path
import argparse,json,struct,time,zlib
VERSION='integrated-0.4.1-gyro-fix'
def image_from_uf2(path, board="pico_w"):
 family_id,flash_end=(0xe48bff56,0x101fd000) if board=="pico_w" else (0xe48bff59,0x103fc000)
 data=path.read_bytes(); assert data and len(data)%512==0
 blocks=[]
 for offset in range(0,len(data),512):
  b=data[offset:offset+512];m0,m1,flags,addr,n,seq,count,family=struct.unpack_from('<8I',b)
  assert (m0,m1,struct.unpack_from('<I',b,508)[0])==(0x0a324655,0x9e5d5157,0x0ab16f30)
  if board=="pico2_w" and family==0xe48bff57 and flags==0xa000: continue
  assert family==family_id and flags&0x2000 and n==256
  assert 0x10000000<=addr<addr+n<=flash_end
  blocks.append((seq,count,addr,b[32:32+n]))
 assert len(blocks)==blocks[0][1] and [b[0] for b in blocks]==list(range(len(blocks)))
 image=bytearray(max(b[2]+256 for b in blocks)-0x10000000)
 for _,_,addr,payload in blocks:image[addr-0x10000000:addr-0x10000000+256]=payload
 return image

def read_device():
 import hid
 for item in hid.enumerate(0x054c,0):
  if item['product_id'] not in (0x0ce6,0x0df2): continue
  d=hid.device()
  try:
   d.open_path(item['path'])
   version=bytes(d.get_feature_report(0xf8,64))[1:].split(b'\0',1)[0].decode('ascii',errors='replace')
   if version!=VERSION:d.close();continue
   raw=bytes(d.get_feature_report(0xf9,64))[1:]
   assert raw[14:17]==b'PW\x01', 'Missing Pico W hardware diagnostics'
   synth,motion,cpu,uptime,size,crc=struct.unpack_from('<6I',raw,18)
   return d,dict(version=version,synth_32_frames_max_us=synth,motion_3_samples_max_us=motion,
                 cpu_khz=cpu,uptime_ms=uptime,image_bytes=size,image_crc32=crc,
                 benchmark_over_budget=bool(raw[17]&8),bluetooth_connected=bool(raw[17]&2),switch_mode=bool(raw[17]&1))
  except (OSError,ValueError,AssertionError):d.close()
 return None,None

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--board',choices=('pico_w','pico2_w'),default='pico_w')
 ap.add_argument('--uf2',type=Path,required=True);ap.add_argument('--output',type=Path)
 args=ap.parse_args();image=image_from_uf2(args.uf2,args.board)
 deadline=time.monotonic()+25
 while time.monotonic()<deadline:
  d,report=read_device()
  if d is not None:break
  time.sleep(0.5)
 else:raise SystemExit('New Pico W firmware did not enumerate/respond within 25 seconds')
 try:
  assert report['cpu_khz']==(125000 if args.board=='pico_w' else 150000)
  assert 0<report['image_bytes']<=len(image)
  expected=zlib.crc32(image[:report['image_bytes']])
  assert expected==report['image_crc32'],(hex(expected),hex(report['image_crc32']))
  assert not report['benchmark_over_budget'],report
  assert report['synth_32_frames_max_us']>0 and report['motion_3_samples_max_us']>0
  report['flash_crc_matches_uf2']=True
  report['verified_at_host_unix']=time.time()
  if args.output:args.output.write_text(json.dumps(report,indent=2)+'\n')
  print(json.dumps(report,indent=2))
 finally:d.close()
if __name__=='__main__':main()

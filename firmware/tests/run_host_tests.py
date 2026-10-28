"""Compile and execute portable protocol, DSP, trigger and transport tests."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/host-tests'
GROUPS={
 'output_mode':['src/usb_mode.cpp','tests/test_output_mode.cpp'],
 'switch2_wake_packet':['src/switch2_wake_packet.cpp','tests/test_switch2_wake_packet.cpp'],
 'switch2_wake_runtime':['src/switch2_wake.cpp','src/switch2_wake_packet.cpp','tests/test_switch2_wake_runtime.cpp'],
 'wireless_haptics':['src/switch_haptics_synth.cpp','src/haptic_calibration.cpp','src/switch_rumble_adapter.cpp','tests/test_wireless_haptics.cpp'],
 'wireless_runtime':['src/trigger_engine.cpp','src/effects_runtime.cpp','src/state_mgr.cpp','tests/test_wireless_runtime.cpp'],
 'motion_calibration':['src/motion_calibration.cpp','tests/test_motion_calibration.cpp'],
 'isolation':['src/switch_pro_usb.cpp','src/switch_pro_protocol.cpp','src/motion_resampler.cpp','src/switch_rumble.cpp','tests/test_mode_isolation.cpp'],
 'interpolation':['src/motion_resampler.cpp','src/haptic_calibration.cpp','src/switch_haptics_synth.cpp','tests/test_interpolation.cpp'],
 'switch':['src/dualsense_parser.cpp','src/switch_pro_protocol.cpp','src/motion_resampler.cpp','src/switch_rumble.cpp','src/switch_haptics_synth.cpp','src/haptic_calibration.cpp','tests/test_switch_pro.cpp'],
 'trigger':['src/trigger_engine.cpp','tests/test_trigger_engine.cpp'],
 'adapter':['src/switch_haptics_synth.cpp','src/haptic_calibration.cpp','src/switch_rumble_adapter.cpp','tests/test_haptics_adapter.cpp'],
 'flash':['src/trigger_engine.cpp','src/config.cpp','src/switch2_wake_packet.cpp','tests/test_flash_storage.cpp'],
 'runtime':['src/trigger_engine.cpp','src/effects_runtime.cpp','src/state_mgr.cpp','tests/test_effects_runtime.cpp']}

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 devcmd=None
 if os.name=='nt' and not shutil.which('cl'):
  base=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio'
  devcmd=next(iter(base.glob('*/*/Common7/Tools/VsDevCmd.bat')),None)
 msvc=os.name=='nt' and (shutil.which('cl') or devcmd)
 commands=[]
 if devcmd: commands.append('@call '+subprocess.list2cmdline([str(devcmd),'-arch=x64','-host_arch=x64'])+' >nul')
 for name,sources in GROUPS.items():
  exe=OUT/(name+('.exe' if os.name=='nt' else ''))
  if msvc:
   args=['cl','/nologo','/utf-8','/std:c++20','/EHsc','/W4','/WX','/wd4146','/wd4103', '/I'+str(ROOT/'src'),'/Fo'+str(OUT)+'/', '/Fe:'+str(exe)]
   if name in ('runtime','wireless_runtime','wireless_haptics','adapter','flash','isolation','switch2_wake_runtime','output_mode'): args+=['/I'+str(ROOT/'tests/stubs'),'/FIhost_compat.h']
  else:
   args=[os.environ.get('CXX','g++'),'-std=c++20','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),'-o',str(exe)]
   if name in ('runtime','wireless_runtime','wireless_haptics','adapter','flash','isolation','output_mode'): args+=['-I'+str(ROOT/'tests/stubs')]
  if name in ('wireless_runtime','wireless_haptics','output_mode'): args+=['/DSWITCH_BT_WIRELESS=1' if msvc else '-DSWITCH_BT_WIRELESS=1']
  args += [str(ROOT/s) for s in sources]
  if name == 'switch2_wake_runtime':
   args += [('/I' if msvc else '-I')+str(ROOT/'tests/wake_stubs'),('/I' if msvc else '-I')+str(ROOT/'tests/stubs')]
  if msvc:
   commands += ['@'+subprocess.list2cmdline(args),'@if %errorlevel% neq 0 exit /b 1','@'+subprocess.list2cmdline([str(exe)]),'@if %errorlevel% neq 0 exit /b 1']
  else:
   subprocess.run(args,check=True,cwd=OUT)
   subprocess.run([str(exe)],check=True,cwd=OUT)
 if msvc:
  batch=OUT/'run.cmd'; batch.write_text('\n'.join(commands)+'\n')
  subprocess.run(['cmd','/c',str(batch)],check=True,cwd=OUT)
 subprocess.run([sys.executable,'-B',str(ROOT/'tests/test_memory_budget.py')],check=True)
 subprocess.run([sys.executable,'-B',str(ROOT/'tests/test_trigger_config.py')],check=True)
 subprocess.run([sys.executable,'-B',str(ROOT/'tests/test_switch2_wake_tool.py')],check=True)
 print('All host tests passed. Hardware tests were not run.')
if __name__=='__main__': main()

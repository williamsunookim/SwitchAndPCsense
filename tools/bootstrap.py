"""Workspace-only Windows build dependencies; no installers or global PATH edits."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request
import zipfile
ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / 'tools'
LOCK = json.loads((ROOT/'DEPENDENCIES.lock.json').read_text())

def run(*args):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True)

def git(path,*args):
    run('git','-c','safe.directory='+str(path).replace('\\','/'),'-C',path,*args)

def fetch_repo(path,url,commit):
    if not (path/'.git').exists():
        path.mkdir(parents=True,exist_ok=True)
        git(path,'init')
        git(path,'remote','add','origin',url)
        git(path,'fetch','--depth','1','origin',commit)
        git(path,'checkout','--detach','FETCH_HEAD')
    found = subprocess.check_output(['git','-c','safe.directory='+str(path).replace('\\','/'),'-C',str(path),'rev-parse','HEAD'],text=True).strip()
    if found != commit:
        raise RuntimeError(f'{path}: expected {commit}, found {found}; use a separate clean tools directory')

def ensure_libraries():
    for key,url in [('opus','https://github.com/xiph/opus.git'),('wdl','https://github.com/justinfrankel/WDL.git')]:
        fetch_repo(ROOT/LOCK[key]['path'],url,LOCK[key]['commit'])

def main():
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--libraries-only',action='store_true',help='Fetch only pinned Opus/WDL sources')
    args=parser.parse_args()
    if args.libraries_only:
        ensure_libraries()
        print('Pinned Opus/WDL sources ready')
        return

    TOOLS.mkdir(exist_ok=True)
    if not (TOOLS/'python/cmake/data/bin/cmake.exe').exists() or not (TOOLS/'python/bin/ninja.exe').exists():
        run(sys.executable,'-m','pip','install','--target',TOOLS/'python','cmake==3.31.6','ninja==1.11.1.3')
    if not (TOOLS/'arm/bin/arm-none-eabi-gcc.exe').exists():
        archive=TOOLS/'downloads/arm.zip'; archive.parent.mkdir(exist_ok=True)
        if not archive.exists():
            urllib.request.urlretrieve('https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-mingw-w64-x86_64-arm-none-eabi.zip',archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != LOCK['toolchain']['sha256']:
            raise RuntimeError('ARM archive checksum mismatch')
        target=TOOLS/'arm'; target.mkdir(exist_ok=True)
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                resolved=(target/name).resolve()
                if target.resolve() not in resolved.parents:
                    raise RuntimeError('Archive path outside tools/arm')
            z.extractall(target)
    sdk=TOOLS/'pico-sdk'
    fetch_repo(sdk,'https://github.com/raspberrypi/pico-sdk.git',LOCK['pico_sdk']['commit'])
    git(sdk,'submodule','update','--init','--depth','1','lib/btstack','lib/cyw43-driver','lib/mbedtls','lib/tinyusb','lib/lwip')
    tiny=sdk/'lib/tinyusb'
    git(tiny,'fetch','--depth','1','origin',LOCK['tinyusb']['commit'])
    git(tiny,'checkout','--detach',LOCK['tinyusb']['commit'])
    ensure_libraries()
    print('Pinned build dependencies ready under',TOOLS)
if __name__=='__main__': main()

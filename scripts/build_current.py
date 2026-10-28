#!/usr/bin/env python3
"""Reproducible isolated build; never changes a running VM or system packages."""
import argparse
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--configure', action='store_true')
parser.add_argument('--target', nargs='+', default=['sunshine','reims_gio_smoke','reims_av_probe'])
args=parser.parse_args()
logs=root/'bridge/run/build-logs'
logs.mkdir(parents=True,exist_ok=True)
base=['docker','run','--rm','--user','1000:1000','--mount',f'type=bind,src={root},dst=/src',
      '-w','/src','-e','HOME=/src','-e','PYTHONPATH=/src/.python','reims-sunshine-builder:local']
commands=[]
if args.configure:commands.append(('configure',['bash','bridge/scripts/configure-sunshine.sh']))
commands.append(('build',['cmake','--build','build-sunshine','--target',*args.target,'--parallel','5']))
for name, cmd in commands:
    with (logs/(name+'.log')).open('w') as f:
        result=subprocess.run(base+cmd,stdout=f,stderr=subprocess.STDOUT,timeout=500,check=False)
    print((logs/(name+'.log')).read_text()[-4500:])
    if result.returncode:raise SystemExit(result.returncode)
print('BUILD_CURRENT_PASS')

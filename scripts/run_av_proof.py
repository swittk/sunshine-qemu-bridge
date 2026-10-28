#!/usr/bin/env python3
"""Capture actual video and generated PCM from the disposable reims-av-fixture."""
import json
import socket
import subprocess
import time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
fixture='reims-av-fixture'
info=json.loads(subprocess.check_output(['docker','inspect',fixture],text=True))[0]
if info['HostConfig']['NetworkMode']!='none' or info['Config']['Image']!='sequoia-reims-metal-profile:local':
    raise SystemExit('Fixture identity changed')
mounts=[m['Source'] for m in info['Mounts']]
if any('/Sequoia-Reims-VM/storage' in p for p in mounts):
    raise SystemExit('Refusing to interact with a production guest disk')
state=root/'bridge/run/av-fixture'
proof=root/'bridge/run/av-proof'
proof.mkdir(parents=True,exist_ok=True)
cmd=['docker','run','--rm','--name','reims-av-capture','--pid','container:'+fixture,
     '--network','none','--user','1000:1000','--mount',f'type=bind,src={root},dst=/src',
     '-w','/src','--entrypoint','python3','reims-sunshine-builder:local',
     '/src/bridge/scripts/qmp_broker.py','--qmp','/src/bridge/run/av-fixture/qmp.sock',
     '--socket','/src/bridge/run/av-fixture/broker.sock','--exec',
     '/src/build-sunshine/reims_av_probe','/src/bridge/run/av-proof']
sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
sock.settimeout(3)
sock.connect(str(state/'qtest.sock'))
reader=sock.makefile('rb')
def command(text):
    sock.sendall(text.encode()+b'\n')
    reply=reader.readline().decode().strip()
    if not reply.startswith('OK'): raise RuntimeError('Qtest: '+reply)
    return reply
original=int(command('inb 0x61').split()[1],0)
with (proof/'capture.log').open('w') as log:
    process=subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT)
    try:
        time.sleep(1.5)
        command('outb 0x43 0xb6')
        divisor=round(1193182/440)
        command('outb 0x42 '+hex(divisor&255))
        command('outb 0x42 '+hex(divisor>>8))
        command('outb 0x61 '+hex(original|3))
        code=process.wait(timeout=20)
    finally:
        command('outb 0x61 '+hex(original&~3))
        sock.close()
        if process.poll() is None:
            process.terminate();process.wait(timeout=5)
print((proof/'capture.log').read_text()[-3500:])
if code:raise SystemExit(code)
print((proof/'result.json').read_text())

import os, subprocess, time
from pathlib import Path
r=Path(__file__).resolve().parents[2]; e=dict(os.environ,PULSE_SERVER='unix:'+str(r/'bridge/run/pulse/native'))
a=r/'bridge/run/bus-source-proof/audio.f32le'
with (r/'bridge/run/moonlight-proof/pulse-control.f32le').open('wb') as f:
    rec=subprocess.Popen(['timeout','9','parec','-d','reims_qemu.monitor','--format=float32le','--rate=48000','--channels=2','--raw'],env=e,stdout=f)
    time.sleep(1); subprocess.run(['pacat','-p','-d','reims_qemu','--format=float32le','--rate=48000','--channels=2','--raw',str(a)],env=e,timeout=15,check=True)
    rec.wait(timeout=15)

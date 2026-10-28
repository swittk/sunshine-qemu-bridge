import os, subprocess, time, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]; env=dict(os.environ,PULSE_SERVER='unix:'+str(root/'bridge/run/pulse/native'))
subprocess.Popen(['python3','bridge/scripts/fixture_tone.py']); time.sleep(2)
with (root/'bridge/run/moonlight-proof/pulse-monitor.f32le').open('wb') as raw:
    monitor=subprocess.Popen(['timeout','12','parec','--device=reims_qemu.monitor','--format=float32le','--rate=48000','--channels=2','--raw'],env=env,stdout=raw)
    subprocess.run(['docker','start','-a',(sys.argv[1] if len(sys.argv)>1 else 'reims-moonlight-proof')],timeout=35,check=False)
    monitor.wait(timeout=5)

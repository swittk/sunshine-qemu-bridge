#!/usr/bin/env python3
"""Package the current build and stage only runtime assets (no credentials)."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
r=Path(__file__).resolve().parents[2]
dist=r/'bridge/dist'
(dist/'assets').mkdir(parents=True,exist_ok=True)
shutil.copy2(r/'build-sunshine/sunshine',dist/'sunshine')
shutil.copy2(r/'build-sunshine/reims_audio_forwarder',dist/'reims_audio_forwarder')
for entry in (r/'build-sunshine/assets').iterdir():
    if entry.name=='shaders':continue
    target=dist/'assets'/entry.name
    if entry.is_dir():shutil.copytree(entry,target,dirs_exist_ok=True)
    elif entry.is_file():shutil.copy2(entry,target)
shutil.copytree(r/'Sunshine/src_assets/linux/assets/shaders',dist/'assets/shaders',dirs_exist_ok=True)
shutil.copy2(r/'Sunshine/LICENSE',dist/'assets/LICENSE.Sunshine')
shutil.copy2(r/'qsm-rd/qsm-direct/LICENSE',dist/'assets/LICENSE.QSM-Direct')
manifest={'binary_sha256':hashlib.sha256((dist/'sunshine').read_bytes()).hexdigest(),
          'sunshine_upstream':'7974a409bb82f2e52a45dad901acdd10fd1d61c1',
          'qsm_direct_upstream':'eb4b652cab71874c264204e8e4463496ed5d7786',
          'experimental':True,'paired_playback_verified':False}
(dist/'assets/streambridge-build.json').write_text(json.dumps(manifest,indent=2)+'\n')
log=r/'bridge/run/build-logs/runtime-image.log'
with log.open('w') as f:
    result=subprocess.run(['docker','build','-f','bridge/Dockerfile.runtime','-t','reims-sunshine-sidecar:local','bridge'],cwd=r,stdout=f,stderr=subprocess.STDOUT,timeout=300)
print(log.read_text()[-3000:]);print(json.dumps(manifest,indent=2))
raise SystemExit(result.returncode)

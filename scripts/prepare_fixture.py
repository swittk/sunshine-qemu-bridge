#!/usr/bin/env python3
"""Prepare a new isolated Compose fixture; preserve any existing configuration."""
from pathlib import Path
import os
r=Path(__file__).resolve().parents[1]
for folder in ['compose-ipc','compose-config']:
    path=r/'run'/folder
    path.mkdir(parents=True,exist_ok=True,mode=0o700)
passwd=r/'run/compose-passwd'
if not passwd.exists():passwd.write_text('server:x:1000:1000:Fixture:/tmp:/usr/sbin/nologin\n')
config=r/'run/compose-config/sunshine.conf'
if not config.exists():
    config.write_text('capture = qemu\nencoder = nvenc\noutput_name = QEMU-0\nstream_audio = disabled\nupnp = disabled\nsunshine_name = ReiMS-QEMU-Fixture\nmin_log_level = info\n')
    os.chmod(config,0o600)
print('Fixture configuration prepared; existing accounts/configuration were not changed')

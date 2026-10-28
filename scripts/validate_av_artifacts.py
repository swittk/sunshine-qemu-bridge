#!/usr/bin/env python3
"""Measure recorded QEMU PCM and round-trip real capture pixels through NVENC."""
import json
import subprocess
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[2]
p=root/'bridge/run/av-proof'
meta=json.loads((p/'result.json').read_text())
frames=meta['video_frames'];w=meta['width'];h=meta['height'];ch=meta['audio_channels'];rate=meta['audio_rate']
audio=np.fromfile(p/'audio.f32le',dtype='<f4').reshape(-1,ch)
mono=audio.mean(axis=1)
active=np.flatnonzero(np.abs(mono)>0.001)
if len(active)<rate:raise SystemExit('Audio proof failed: fewer than one second of non-silent samples')
start=int(active[0]);segment=mono[start:start+rate*2]
spectrum=np.abs(np.fft.rfft(segment*np.hanning(len(segment))))
freq=np.fft.rfftfreq(len(segment),1/rate)
mask=(freq>300)&(freq<600)
peak=float(freq[mask][np.argmax(spectrum[mask])])
if abs(peak-440)>4:raise SystemExit(f'Unexpected generated-tone peak {peak}')
assert np.isfinite(audio).all()

# CPU pixel conversion is deliberately separate from the hardware encoder.
# Both source and decoded frames are checked, not merely an encoder name list.
subprocess.run(['ffmpeg','-y','-hide_banner','-loglevel','error','-f','rawvideo','-pixel_format','bgra',
 '-video_size',f'{w}x{h}','-framerate','60','-i',str(p/'video.bgra'),'-pix_fmt','nv12','-f','rawvideo',str(p/'input.nv12')],check=True,timeout=60)
ff='/src/build-sunshine/_deps/ffmpeg-v2026.1017.131201/ffmpeg/bin/ffmpeg'
cmd=['docker','run','--rm','--user','1000:1000','--device','nvidia.com/gpu=GPU-020906b4-0904-3471-e4ee-e41ec2350be3',
     '-e','NVIDIA_DRIVER_CAPABILITIES=video,compute,utility','--mount',f'type=bind,src={root},dst=/src',
     '--entrypoint',ff,'reims-sunshine-builder:local','-y','-hide_banner','-loglevel','info',
     '-f','rawvideo','-pixel_format','nv12','-video_size',f'{w}x{h}','-framerate','60',
     '-i','/src/bridge/run/av-proof/input.nv12','-c:v','h264_nvenc','-preset','p1','-tune','ull','-b:v','8M',
     '-f','h264','/src/bridge/run/av-proof/nvenc.h264']
with (p/'nvenc.log').open('w') as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
subprocess.run(['ffmpeg','-y','-hide_banner','-loglevel','error','-i',str(p/'nvenc.h264'),
               '-vsync','0','-pix_fmt','bgra','-f','rawvideo',str(p/'decoded.bgra')],check=True,timeout=60)
raw=np.memmap(p/'video.bgra',dtype=np.uint8,mode='r').reshape(-1,h,w,4)
dec=np.memmap(p/'decoded.bgra',dtype=np.uint8,mode='r').reshape(-1,h,w,4)
if len(dec)!=frames:raise SystemExit(f'Expected {frames} decoded frames, got {len(dec)}')
first=raw[0,:,:,:3].astype(float);last=dec[0,:,:,:3].astype(float)
mae=float(np.abs(first-last).mean())
assert first.std()>5 and last.std()>5, 'No meaningful frame content'
assert mae<12, f'Excessive decoded pixel error: {mae}'
result={**meta,'pcm_nonzero_samples':int(len(active)), 'tone_peak_hz':peak,
 'pcm_peak':float(np.abs(audio).max()),'pcm_rms':float(np.sqrt(np.mean(audio**2))),
 'decoded_video_frames':len(dec),'encoded_bytes':(p/'nvenc.h264').stat().st_size,
 'first_frame_rgb_mae':mae,'real_qemu_capture_and_nvenc_roundtrip':True,
 'moonlight_network_playback_tested':False}
(p/'validated.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))

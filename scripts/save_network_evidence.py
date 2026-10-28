import datetime,hashlib,json,subprocess
from pathlib import Path
r=Path(__file__).resolve().parents[2];proof=r/'bridge/run/moonlight-proof';dest=r/'bridge/evidence/network';dest.mkdir(parents=True,exist_ok=True)
a=json.loads((proof/'audio-verified.json').read_text())
probe=subprocess.run(['ffprobe','-v','error','-count_frames','-select_streams','v:0','-show_entries','stream=codec_name,width,height,nb_read_frames','-of','json',str(proof/'received.h264')],capture_output=True,text=True,check=True,timeout=30)
v=json.loads(probe.stdout)['streams'][0];declared=int((proof/'video.txt').read_text().split('frames ')[1])
assert int(v['nb_read_frames'])==declared and declared>100
assert a['end_to_end_audio_verified'] and abs(a['tone_peak_hz']-440)<4
import numpy as np
frame=subprocess.check_output(['ffmpeg','-v','error','-i',str(proof/'received.h264'),'-frames:v','1','-pix_fmt','rgb24','-f','rawvideo','-'],timeout=30)
pixels=np.frombuffer(frame,dtype=np.uint8);assert pixels.size==v['width']*v['height']*3 and pixels.std()>5
hashes={name:hashlib.sha256((proof/name).read_bytes()).hexdigest() for name in ('received.h264','received.opuspackets')}
image=subprocess.check_output(['docker','inspect','reims-sunshine-audio','--format','{{.Image}}'],text=True).strip()
result={'verified_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'video':v,'audio':a,'first_frame_std':float(pixels.std()),'raw_sha256':hashes,'sunshine_image':image,'paired_network_av_verified':True,'headless_receiver':True,'macos_guest_tested':False,'display_latency_tested':False}
(dest/'paired-av.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))

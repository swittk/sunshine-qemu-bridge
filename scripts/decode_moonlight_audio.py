import ctypes as C, ctypes.util, json, struct
from pathlib import Path
import numpy as np
root=Path(__file__).resolve().parents[1]/'run/moonlight-proof'
values=list(map(int,(root/'audio.txt').read_text().split()))
rate,channels,streams,coupled,frame_size=values[:5]
assert rate==48000 and channels==2 and streams==1 and coupled==1
lib=C.CDLL(ctypes.util.find_library('opus'))
lib.opus_multistream_decoder_create.argtypes=[C.c_int,C.c_int,C.c_int,C.c_int,C.POINTER(C.c_ubyte),C.POINTER(C.c_int)]
lib.opus_multistream_decoder_create.restype=C.c_void_p
lib.opus_multistream_decode_float.argtypes=[C.c_void_p,C.c_char_p,C.c_int,C.POINTER(C.c_float),C.c_int,C.c_int]
lib.opus_multistream_decode_float.restype=C.c_int
mapping=(C.c_ubyte*channels)(*values[5:5+channels]); error=C.c_int()
decoder=lib.opus_multistream_decoder_create(rate,channels,streams,coupled,mapping,C.byref(error))
assert decoder and error.value==0
payload=(root/'received.opuspackets').read_bytes(); offset=0; chunks=[]; buffer=(C.c_float*(5760*channels))()
while offset<len(payload):
    assert offset+4<=len(payload)
    length=struct.unpack_from('<I',payload,offset)[0]; offset+=4
    assert 0<length<=65535 and offset+length<=len(payload)
    decoded=lib.opus_multistream_decode_float(decoder,payload[offset:offset+length],length,buffer,5760,0)
    assert decoded>0, decoded
    chunks.append(np.ctypeslib.as_array(buffer)[:decoded*channels].copy()); offset+=length
lib.opus_multistream_decoder_destroy.argtypes=[C.c_void_p];lib.opus_multistream_decoder_destroy(decoder)
pcm=np.concatenate(chunks).reshape(-1,channels); assert np.isfinite(pcm).all()
print('DECODED_PCM',pcm.shape,'peak',float(np.abs(pcm).max()),'rms',float(np.sqrt(np.mean(pcm**2))))
mono=pcm.mean(axis=1); active=np.flatnonzero(np.abs(mono)>.01); assert len(active)>rate
segment=mono[int(active[0])+rate:int(active[0])+rate*3]; spectrum=np.abs(np.fft.rfft(segment*np.hanning(len(segment))))
frequency=np.fft.rfftfreq(len(segment),1/rate); band=(frequency>300)&(frequency<600); peak=float(frequency[band][np.argmax(spectrum[band])])
assert abs(peak-440)<4, peak
pcm.astype('<f4').tofile(root/'decoded-audio.f32le')
result={'moonlight_opus_packets':len(chunks),'decoded_audio_frames':len(pcm),'sample_rate':rate,'channels':channels,'tone_peak_hz':peak,'rms':float(np.sqrt(np.mean(pcm**2))),'peak':float(np.abs(pcm).max()),'non_silent_samples':int(len(active)),'end_to_end_audio_verified':True}
(root/'audio-verified.json').write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result,indent=2))

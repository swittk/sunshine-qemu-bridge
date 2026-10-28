#include <stdio.h>
#include <stdlib.h>
static FILE *proof_video, *proof_audio, *proof_meta;
static unsigned proof_frames, proof_packets;
static FILE* proof_open(const char* name) {
 const char* directory=getenv("REIMS_PROOF_DIR"); char path[1024];
 if(!directory || snprintf(path,sizeof(path),"%s/%s",directory,name)>=(int)sizeof(path)) return NULL;
 return fopen(path,"wb"); }
static void proof_video_init(int format,int w,int h,int fps) {
 proof_video=proof_open("received.h264"); proof_frames=0;
 proof_meta=proof_open("video.txt"); if(proof_meta){fprintf(proof_meta,"%d %d %d %d\n",format,w,h,fps);fflush(proof_meta);}
}
static void proof_video_unit(PDECODE_UNIT unit) {
 if(!proof_video || proof_frames>=3600)return;
 for(PLENTRY p=unit->bufferList;p;p=p->next)fwrite(p->data,1,p->length,proof_video);
 ++proof_frames;fflush(proof_video); }
static void proof_audio_init(POPUS_MULTISTREAM_CONFIGURATION cfg) {
 FILE* file=proof_open("audio.txt"); proof_audio=proof_open("received.opuspackets");proof_packets=0;
 if(file){fprintf(file,"%d %d %d %d %d\n",cfg->sampleRate,cfg->channelCount,cfg->streams,cfg->coupledStreams,cfg->samplesPerFrame);for(int c=0;c<cfg->channelCount;c++)fprintf(file,"%u ",cfg->mapping[c]);fclose(file);}
}
static void proof_audio_packet(const char* data,int length) {
 if(!proof_audio || length<1 || length>65535 || proof_packets>=40000)return;
 unsigned char prefix[4]={(unsigned char)length,(unsigned char)(length>>8),0,0};
 fwrite(prefix,1,4,proof_audio);fwrite(data,1,length,proof_audio);fflush(proof_audio);++proof_packets; }
static void proof_video_close(void){if(proof_video){fclose(proof_video);proof_video=NULL;}if(proof_meta){fprintf(proof_meta,"frames %u\n",proof_frames);fclose(proof_meta);proof_meta=NULL;}}
static void proof_audio_close(void){if(proof_audio){fclose(proof_audio);proof_audio=NULL;}fprintf(stderr,"PROOF_AUDIO_PACKETS=%u\n",proof_packets);}

#include <cstdio>
#include "pulse_audio_bridge.hpp"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <utility>

namespace reims_stream {
PulseAudioBridge::~PulseAudioBridge(){ stop(); }
void PulseAudioBridge::start(){
 if(active_.exchange(true)) return;
 worker_=std::thread(&PulseAudioBridge::worker,this);
}
void PulseAudioBridge::stop() noexcept {
 if(!active_.exchange(false)) return;
 cv_.notify_all();
 if(worker_.joinable()) worker_.join();
}
void PulseAudioBridge::submit(std::span<const float> pcm,
 std::uint32_t rate,std::uint16_t channels) {
 if(!active_) return;
 if(queue_.push(pcm,rate,channels)) cv_.notify_one();
}
PulseAudioBridge::Stats PulseAudioBridge::stats() const {
 return {queue_.stats(),connection_errors_.load(),playback_errors_.load(),played_chunks_.load()};
}
void PulseAudioBridge::worker() noexcept {
 // PulseAudio handles the source resampling. Queue remains bounded even if
 // PipeWire stops responding. Never block the QEMU Display1 callback thread.
 struct Deleter{void operator()(pa_simple* p)const noexcept {if(p)pa_simple_free(p);}};
 std::unique_ptr<pa_simple,Deleter> sink;
 std::uint32_t current_rate=0;
 std::uint16_t current_channels=0;
 while(active_) {
  auto chunk=queue_.pop();
  if(!chunk) {
    std::unique_lock lock(mutex_);
    cv_.wait_for(lock,std::chrono::milliseconds(20),[this]{return !active_||queue_.stats().pending>0;});
    continue;
  }
  if(!sink || current_rate!=chunk->sample_rate || current_channels!=chunk->channels) {
    sink.reset();
    current_rate=chunk->sample_rate;
    current_channels=chunk->channels;
    pa_sample_spec spec{PA_SAMPLE_FLOAT32LE,chunk->sample_rate,static_cast<std::uint8_t>(chunk->channels)};
    pa_buffer_attr attr{};
    attr.maxlength=(std::uint32_t)-1;
    attr.tlength=std::max<std::uint32_t>(4096U,chunk->sample_rate*chunk->channels*sizeof(float)/25U); // ~40ms
    attr.prebuf=chunk->sample_rate*chunk->channels*sizeof(float)/50U; // Rebuffer 20ms after silence/underrun.
    attr.minreq=(std::uint32_t)-1;
    attr.fragsize=(std::uint32_t)-1;
    const char *sink_name=std::getenv("REIMS_AUDIO_SINK");
    if(sink_name && !*sink_name) sink_name=nullptr;
    int error=PA_OK;
    sink.reset(pa_simple_new(nullptr,"ReiMS QEMU Bridge",PA_STREAM_PLAYBACK,
      sink_name,"Guest audio",&spec,nullptr,&attr,&error));
    if(!sink) {
      ++connection_errors_; fprintf(stderr,"REIMS_PULSE_OPEN_ERROR=%s\n",pa_strerror(error));
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      continue;
    }
  }
  int err=PA_OK;
  if(pa_simple_write(sink.get(),chunk->samples.data(),
       chunk->samples.size()*sizeof(float),&err)<0) {
    ++playback_errors_;
    sink.reset();
    continue;
  }
  ++played_chunks_;
 }
}
} // namespace reims_stream

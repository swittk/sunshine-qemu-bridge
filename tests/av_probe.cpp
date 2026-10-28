#include "gio_qemu_display.hpp"
#include "capture_feed.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
int main(int argc,char** argv) {
  if(argc!=2) {std::cerr<<"Usage: reims_av_probe OUTPUT_DIRECTORY (QEMU_DBUS_ADDRESS required)\n";return 2;}
  const char* address=std::getenv("QEMU_DBUS_ADDRESS");
  const char* dest=std::getenv("QEMU_DBUS_DESTINATION");
  if(!address) return 2;
  const std::filesystem::path out(argv[1]);
  std::filesystem::create_directories(out);
  std::ofstream audio(out/"audio.f32le",std::ios::binary);
  std::ofstream video(out/"video.bgra",std::ios::binary);
  std::atomic<std::uint64_t> audio_samples{0},audio_callbacks{0};
  std::atomic<std::uint32_t> rate{0},channels{0};
  auto display=std::make_shared<reims_stream::GioQemuDisplay>(address,dest?dest:"org.qemu",true);
  reims_stream::CaptureFeed feed(*display);
  feed.set_audio_callback([&](std::span<const float> pcm,std::uint32_t hz,std::uint16_t ch){
    if(audio_samples.load()>48000U*2U*15U) return;
    rate=hz;channels=ch;
    audio.write(reinterpret_cast<const char*>(pcm.data()),pcm.size_bytes());
    audio_samples+=pcm.size();++audio_callbacks;
  });
  try {
    feed.start();
    std::this_thread::sleep_for(750ms); // settle QEMU placeholder -> actual scanout
    auto geometry=feed.wait_geometry(5s);
    if(!geometry) throw std::runtime_error("no QEMU video frame");
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(geometry->width)*geometry->height*4U);
    reims_stream::FrameMetadata meta;
    const auto started=std::chrono::steady_clock::now();
    const auto deadline=started+5s;
    auto next=started;
    std::uint64_t frames=0;
    double age_sum=0,age_max=0;
    while(std::chrono::steady_clock::now()<deadline) {
      auto status=feed.next_bgra(buffer,geometry->width,geometry->height,200ms,meta);
      if(status==reims_stream::CaptureStatus::resize) throw std::runtime_error("fixture changed resolution");
      if(status==reims_stream::CaptureStatus::error) throw std::runtime_error(feed.stats().latest_error);
      if(status==reims_stream::CaptureStatus::frame) {
        video.write(reinterpret_cast<const char*>(buffer.data()),buffer.size());
        double age=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-meta.produced_at).count();
        age_sum+=age;age_max=std::max(age_max,age);++frames;
        if(frames==1){
          std::ofstream ppm(out/"first-frame.ppm",std::ios::binary);
          ppm<<"P6\n"<<geometry->width<<' '<<geometry->height<<"\n255\n";
          for(std::size_t i=0;i<buffer.size();i+=4){const char rgb[]{(char)buffer[i+2],(char)buffer[i+1],(char)buffer[i]};ppm.write(rgb,3);}
        }
      }
      next+=std::chrono::microseconds(16667);
      std::this_thread::sleep_until(next);
    }
    const auto a=display->audio_stats();
    feed.stop();audio.close();video.close();
    const auto stats=feed.stats();
    std::ofstream json(out/"result.json");
    json<<"{\n  \"width\": "<<geometry->width<<", \"height\": "<<geometry->height
        <<", \"video_frames\": "<<frames<<", \"audio_samples\": "<<audio_samples.load()
        <<", \"audio_callbacks\": "<<audio_callbacks.load()<<", \"audio_rate\": "<<rate.load()
        <<", \"audio_channels\": "<<channels.load()<<", \"audio_connected\": "<<(a.connected?"true":"false")
        <<", \"audio_errors\": "<<a.errors<<", \"connection_errors\": "<<stats.connection_errors
        <<", \"mean_capture_age_ms\": "<<(frames?age_sum/frames:0)
        <<", \"max_capture_age_ms\": "<<age_max<<", \"mailbox_dropped\": "<<stats.mailbox.dropped<<"\n}\n";
    std::cout<<"AV_PROBE frames="<<frames<<" audio_samples="<<audio_samples.load()<<" errors="<<stats.connection_errors<<"\n";
    return frames>5 && stats.connection_errors==0 ? 0 : 1;
  } catch(const std::exception& e) {
    feed.stop();std::cerr<<"AV_PROBE_FAILED "<<e.what()<<'\n';return 1;
  }
}

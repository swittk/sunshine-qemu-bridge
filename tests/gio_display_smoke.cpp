#include "gio_qemu_display.hpp"
#include "capture_feed.hpp"
#include <chrono>
#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace std::chrono_literals;
int main(int argc,char** argv){
 if(argc<2){std::cerr<<"Usage: reims_gio_smoke DBUS_ADDRESS [DESTINATION]\n";return 2;}
 auto dest=argc>=3?std::string(argv[2]):std::string("org.qemu");
 const char* address=std::string(argv[1])=="env" ? std::getenv("QEMU_DBUS_ADDRESS") : argv[1];
 if(!address){std::cerr<<"QEMU_DBUS_ADDRESS not set\n";return 2;}
 auto display=std::make_shared<reims_stream::GioQemuDisplay>(address,dest,true);
 reims_stream::CaptureFeed feed(*display);
 std::atomic<std::uint64_t> audio_callbacks{0};
 feed.set_audio_callback([&audio_callbacks](std::span<const float>,std::uint32_t,std::uint16_t){++audio_callbacks;});
 try{
  feed.start();
  auto geom=feed.wait_geometry(9s);
  if(!geom){std::cerr<<"GIO_FRAME_NO_GEOMETRY "<<feed.stats().latest_error<<"\n";return 3;}
  const auto bytes=static_cast<std::size_t>(geom->width)*geom->height*4U;
  std::vector<std::uint8_t> pixels(bytes);
  reims_stream::FrameMetadata meta;
  std::uint64_t frames=0;
  for(int i=0;i<60;++i){
   auto status=feed.next_bgra(pixels,geom->width,geom->height,200ms,meta);
   if(status==reims_stream::CaptureStatus::resize){
    geom=feed.wait_geometry(100ms);
    if(!geom) break;
    pixels.resize(static_cast<std::size_t>(geom->width)*geom->height*4U);
    continue;
   }
   if(status==reims_stream::CaptureStatus::frame) ++frames;
   if(status==reims_stream::CaptureStatus::error || status==reims_stream::CaptureStatus::interrupted) break;
  }
  // In an isolated test guest only, exercise the actual QEMU input path.
  display->key(30,true);display->key(30,false);
  display->relative_pointer(12,-6);
  display->button(0,true);display->button(0,false);
  const auto stats=feed.stats();
  std::cout<<"REIMS_GIO_RESULT resolution="<<geom->width<<"x"<<geom->height
      <<" frames="<<frames<<" dropped="<<stats.mailbox.dropped
      <<" errors="<<stats.connection_errors
      <<" audio_connected="<<display->audio_stats().connected
      <<" audio_errors="<<display->audio_stats().errors
      <<" audio_writes="<<audio_callbacks.load()
      <<" error="<<stats.latest_error<<"\n";
  feed.stop();
  return frames>=1?0:4;
 }catch(const std::exception& ex){
  std::cerr<<"REIMS_GIO_ERROR "<<ex.what()<<"\n";
  return 1;
 }
}

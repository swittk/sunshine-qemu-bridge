#include "gio_qemu_display.hpp"
#include "capture_feed.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
int main() {
 const char* addr=std::getenv("QEMU_DBUS_ADDRESS");
 const char* dest=std::getenv("QEMU_DBUS_DESTINATION");
 if(!addr)return 2;
 const auto began=std::chrono::steady_clock::now();
 for(int i=0;i<12;++i){
  try {
   reims_stream::GioQemuDisplay source(addr,dest?dest:"",true);
   reims_stream::CaptureFeed feed(source);
   feed.start();
   if(i%3==0){
      if(!feed.wait_geometry(3s))throw std::runtime_error("reconnect delivered no frame");
   } else if(i%3==1)std::this_thread::sleep_for(1ms);
   // Stop immediately after handshake, or while dispatching frames.
   const auto stop_start=std::chrono::steady_clock::now();
   feed.stop();
   const auto stop_ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-stop_start).count();
   if(stop_ms>2000)throw std::runtime_error("shutdown exceeded 2 seconds");
   std::cout<<"LIFECYCLE cycle="<<i<<" stop_ms="<<stop_ms<<'\n';
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 }
 std::cout<<"LIFECYCLE_PASS cycles=12 total_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count()<<'\n';
}

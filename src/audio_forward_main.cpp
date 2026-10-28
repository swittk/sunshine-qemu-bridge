#include "gio_qemu_audio.hpp"
#include "pulse_audio_bridge.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
static volatile std::sig_atomic_t active=1;
int main() {
 const char* address=std::getenv("QEMU_DBUS_ADDRESS");
 if(!address){std::fprintf(stderr,"QEMU_DBUS_ADDRESS is required\n");return 2;}
 const char* dest=std::getenv("QEMU_DBUS_DESTINATION"); if(!dest)dest="org.qemu";
 auto flags=G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT;
 if(*dest)flags=static_cast<GDBusConnectionFlags>(flags|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
 GError* error=nullptr;
 g_autoptr(GDBusConnection) bus=g_dbus_connection_new_for_address_sync(address,flags,nullptr,nullptr,&error);
 if(!bus){std::fprintf(stderr,"Audio bus: %s\n",error?error->message:"failed");if(error)g_error_free(error);return 1;}
 std::signal(SIGTERM,[](int){active=0;});std::signal(SIGINT,[](int){active=0;});
 try {
  reims_stream::PulseAudioBridge pulse;
  reims_stream::GioQemuAudio source(bus,dest,[&](auto pcm,auto rate,auto channels){pulse.submit(pcm,rate,channels);});
  pulse.start();source.start();
  const auto startup=std::chrono::steady_clock::now()+std::chrono::seconds(8);
  while(active){
   if(std::chrono::steady_clock::now()>startup&&!source.stats().connected)throw std::runtime_error("QEMU audio disconnected");
   std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  source.stop();pulse.stop();
 }catch(const std::exception& e){std::fprintf(stderr,"Audio forwarder: %s\n",e.what());return 1;}
 return 0;
}

#include <cstdio>
#include "gio_qemu_audio.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace reims_stream {
namespace {
constexpr auto* xml_text=R"xml(<node>
<interface name="org.qemu.Display1.AudioOutListener">
<method name="Init"><arg type="t"/><arg type="y"/><arg type="b"/><arg type="b"/><arg type="u"/><arg type="y"/><arg type="u"/><arg type="u"/><arg type="b"/></method>
<method name="Fini"><arg type="t"/></method>
<method name="SetEnabled"><arg type="t"/><arg type="b"/></method>
<method name="SetVolume"><arg type="t"/><arg type="b"/><arg type="ay"/></method>
<method name="Write"><arg type="t"/><arg type="ay"/></method>
</interface>
</node>)xml";
std::string message(GError *error, const char* fallback) {
 if(!error) return fallback;
 std::string str=error->message;
 g_error_free(error);
 return str;
}
}
GioQemuAudio::GioQemuAudio(GDBusConnection* connection,std::string destination,AudioCallback callback):
 main_(connection),destination_(std::move(destination)),callback_(std::move(callback)) {
 if(!main_) throw std::invalid_argument("GIO audio main bus missing");
}
GioQemuAudio::~GioQemuAudio(){stop();}
void GioQemuAudio::start() {
 if(running_.exchange(true)) throw std::logic_error("audio already started");
 GError* error=nullptr;
 int pair[2]{-1,-1};
 if(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)<0){
   running_=false;throw std::runtime_error("could not create audio peer");
 }
 g_autoptr(GUnixFDList) fds=g_unix_fd_list_new();
 const int handle=g_unix_fd_list_append(fds,pair[1],&error);
 if(handle<0){
   ::close(pair[0]);::close(pair[1]);running_=false;
   throw std::runtime_error(message(error,"audio FD append failed"));
 }
 g_autoptr(GVariant) reply=g_dbus_connection_call_with_unix_fd_list_sync(
    main_,destination_.empty()?nullptr:destination_.c_str(),
    "/org/qemu/Display1/Audio","org.qemu.Display1.Audio",
    "RegisterOutListener",g_variant_new("(h)",handle),G_VARIANT_TYPE_UNIT,
    G_DBUS_CALL_FLAGS_NONE,2500,fds,nullptr,nullptr,&error);
 ::close(pair[1]);
 if(!reply){
    ::close(pair[0]);running_=false;
    throw std::runtime_error(message(error,"QEMU Audio.RegisterOutListener failed"));
 }
 try {worker_=std::thread(&GioQemuAudio::worker_loop,this,pair[0]);}
 catch(...) {::close(pair[0]);running_=false;throw;}
}
GioQemuAudio::Stats GioQemuAudio::stats() const {
 return {chunks_.load(),frames_.load(),errors_.load(),connected_.load()};
}
void GioQemuAudio::method_call(GDBusConnection*,const gchar*,const gchar*,
 const gchar*,const gchar* name,GVariant* data,GDBusMethodInvocation* invocation,gpointer opaque){
 auto* instance=static_cast<GioQemuAudio*>(opaque);
 instance->handle(name,data,invocation);
}
void GioQemuAudio::closed(GDBusConnection*,gboolean,GError* error,gpointer opaque) {
 std::fprintf(stderr,"AUDIO_PEER_CLOSED: %s\n",error?error->message:"clean");
 auto* self=static_cast<GioQemuAudio*>(opaque);
 self->connected_=false;
 if(self->loop_)g_main_loop_quit(self->loop_);
}
void GioQemuAudio::handle(const char* name,GVariant* data,GDBusMethodInvocation* invocation) {
 try {
    if(std::strcmp(name,"Init")==0) {
      guint64 id=0;guchar bits=0,nchannels=0;gboolean sg=0,fp=0,be=0;
      guint32 freq=0,bpf=0,bps=0;
      g_variant_get(data,"(tybbuyuub)",&id,&bits,&sg,&fp,&freq,&nchannels,&bpf,&bps,&be);
      const auto bytes=(bits+7U)/8U;
      if(freq<8000U||freq>192000U||nchannels==0||nchannels>8||
         !(bits==8||bits==16||bits==24||bits==32||bits==64)||
         (fp&&!(bits==32||bits==64))||bytes*nchannels!=bpf)
           throw std::runtime_error("invalid QEMU audio PCM format");
      std::lock_guard lock(mutex_);
      streams_[id]=Stream{bits,nchannels,sg!=0,fp!=0,be!=0,true,false,
                          freq,bpf,std::vector<std::uint8_t>(nchannels,255U)};
    } else if(std::strcmp(name,"Fini")==0) {
      guint64 id=0;g_variant_get(data,"(t)",&id);
      std::lock_guard lock(mutex_);streams_.erase(id);
    } else if(std::strcmp(name,"SetEnabled")==0) {
      guint64 id=0;gboolean enabled=0;g_variant_get(data,"(tb)",&id,&enabled);
      std::lock_guard lock(mutex_);
      if(auto it=streams_.find(id);it!=streams_.end()) it->second.enabled=enabled!=0;
    } else if(std::strcmp(name,"SetVolume")==0) {
      guint64 id=0;gboolean mute=0;GVariant* val=nullptr;
      g_variant_get(data,"(tb@ay)",&id,&mute,&val);
      gsize len=0;
      const auto* bytes=static_cast<const std::uint8_t*>(g_variant_get_fixed_array(val,&len,1));
      std::lock_guard lock(mutex_);
      if(auto it=streams_.find(id);it!=streams_.end()){
       it->second.mute=mute!=0;
       if(bytes && len==it->second.channels)it->second.volume.assign(bytes,bytes+len);
      }
      g_variant_unref(val);
    } else if(std::strcmp(name,"Write")==0) {
      guint64 id=0;GVariant* val=nullptr;
      g_variant_get(data,"(t@ay)",&id,&val);
      gsize len=0;
      const auto* bytes=static_cast<const std::uint8_t*>(g_variant_get_fixed_array(val,&len,1));
      if(bytes&&len)on_write(id,{bytes,len});
      g_variant_unref(val);
    } else {
      g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,
          G_DBUS_ERROR_NOT_SUPPORTED,"Unsupported audio method");
      return;
    }
    g_dbus_method_invocation_return_value(invocation,g_variant_new("()"));
 } catch(const std::exception& ex) {
   ++errors_;fprintf(stderr,"REIMS_AUDIO_ERROR=%s\n",ex.what());
   g_dbus_method_invocation_return_error(invocation,G_DBUS_ERROR,G_DBUS_ERROR_FAILED,"%s",ex.what());
 }
}
void GioQemuAudio::on_write(std::uint64_t id,std::span<const std::uint8_t> input) {
 Stream cfg;
 {
   std::lock_guard lock(mutex_);
   const auto it=streams_.find(id);
   if(it==streams_.end())return;
   cfg=it->second;
 }
 if(!cfg.enabled||cfg.mute||!cfg.frame_bytes||input.size()%cfg.frame_bytes) return;
 const auto frame_count=input.size()/cfg.frame_bytes;
 if(frame_count>cfg.frequency)return; // reject excessive queueing
 const auto sample_bytes=(cfg.bits+7U)/8U;
 std::vector<float> pcm(frame_count*cfg.channels);
 for(std::size_t i=0;i<pcm.size();++i) {
  const auto* sample=input.data()+i*sample_bytes;
  std::uint64_t raw=0;
  for(std::size_t k=0;k<sample_bytes;++k){
    const auto n=cfg.big_endian?sample_bytes-1U-k:k;
    raw|=static_cast<std::uint64_t>(sample[n]) << (8U*k);
  }
  double value=0;
  if(cfg.float_pcm) {
    if(cfg.bits==32) {
      const auto r=static_cast<std::uint32_t>(raw);
      float f=0;std::memcpy(&f,&r,sizeof(f));value=f;
    } else {
      double f=0;std::memcpy(&f,&raw,sizeof(f));value=f;
    }
  } else {
    const double denom=std::ldexp(1.0,cfg.bits-1);
    if(cfg.signed_pcm) {
      std::int64_t signedval=0;
      if(cfg.bits==64)std::memcpy(&signedval,&raw,sizeof(raw));
      else signedval=(raw & (std::uint64_t{1}<<(cfg.bits-1))) ?
          static_cast<std::int64_t>(raw)-static_cast<std::int64_t>(std::uint64_t{1}<<cfg.bits):
          static_cast<std::int64_t>(raw);
      value=static_cast<double>(signedval)/denom;
    } else value=(static_cast<double>(raw)-denom)/denom;
  }
  if(!std::isfinite(value))value=0;
  value=std::clamp(value,-1.0,1.0);
  const auto vol=cfg.volume.size()==cfg.channels?
                  cfg.volume[i%cfg.channels]/255.0:1.0;
  pcm[i]=static_cast<float>(value*vol);
 }
 if(callback_)callback_(pcm,cfg.frequency,cfg.channels);
 ++chunks_;frames_+=frame_count;
}
void GioQemuAudio::worker_loop(int fd) {
 auto* ctx=g_main_context_new();
 g_main_context_push_thread_default(ctx);
 GError* err=nullptr;
 GSocket* socket=g_socket_new_from_fd(fd,&err);
 if(!socket){++errors_;if(err)g_error_free(err);goto done;}
 {
   g_socket_set_timeout(socket,5);
   GSocketConnection* stream=g_socket_connection_factory_create_connection(socket);
   g_object_unref(socket);
   peer_=g_dbus_connection_new_sync(G_IO_STREAM(stream),nullptr,
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|
           G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING),nullptr,nullptr,&err);
   if(peer_)g_socket_set_timeout(g_socket_connection_get_socket(stream),0); // Only authentication has an idle deadline.
   g_object_unref(stream);
 }
 if(!peer_){++errors_;if(err)g_error_free(err);goto done;}
 xml_=g_dbus_node_info_new_for_xml(xml_text,&err);
 if(!xml_){++errors_;if(err)g_error_free(err);goto done;}
 {
   static const GDBusInterfaceVTable vtable={&GioQemuAudio::method_call,nullptr,nullptr,{0}};
   auto registration=g_dbus_connection_register_object(peer_,
        "/org/qemu/Display1/AudioOutListener",xml_->interfaces[0],&vtable,
        this,nullptr,&err);
   if(!registration){++errors_;if(err)g_error_free(err);goto done;}
   connected_=true;
   g_signal_connect(peer_,"closed",G_CALLBACK(&GioQemuAudio::closed),this);
   loop_=g_main_loop_new(ctx,FALSE);
   g_dbus_connection_start_message_processing(peer_);
   auto* cancel_check=g_timeout_source_new(50);
   g_source_set_callback(cancel_check,+[](gpointer opaque)->gboolean {
     auto* self=static_cast<GioQemuAudio*>(opaque);
     if(!self->running_){g_main_loop_quit(self->loop_);return G_SOURCE_REMOVE;}
     return G_SOURCE_CONTINUE;
   },this,nullptr);
   g_source_attach(cancel_check,ctx);
   if(running_)g_main_loop_run(loop_);
   g_source_destroy(cancel_check);g_source_unref(cancel_check);
 }
done:
 connected_=false;
 if(loop_){g_main_loop_unref(loop_);loop_=nullptr;}
 if(peer_){g_dbus_connection_close_sync(peer_,nullptr,nullptr);g_object_unref(peer_);peer_=nullptr;}
 if(xml_){g_dbus_node_info_unref(xml_);xml_=nullptr;}
 g_main_context_pop_thread_default(ctx);
 g_main_context_unref(ctx);
}
void GioQemuAudio::stop() noexcept {
 if(!running_.exchange(false))return;
 if(worker_.joinable())worker_.join();
}
} // namespace reims_stream

#include "gio_qemu_display.hpp"
#include "core/unix_fd.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace reims_stream {
namespace {
int receive_broker_fd(std::string_view path) {
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path))
        throw std::invalid_argument("invalid QMP broker path");
    const int fd=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    if(fd<0)throw std::runtime_error("cannot open QMP broker socket");
    sockaddr_un addr{};
    addr.sun_family=AF_UNIX;
    std::memcpy(addr.sun_path,path.data(),path.size());
    if(::connect(fd,reinterpret_cast<const sockaddr*>(&addr),
                 offsetof(sockaddr_un,sun_path)+path.size()+1)<0) {
        ::close(fd);throw std::runtime_error("could not connect private QMP broker");
    }
    char byte=0;
    char control[CMSG_SPACE(sizeof(int))]{};
    iovec iov{&byte,1};
    msghdr msg{};
    msg.msg_iov=&iov;msg.msg_iovlen=1;
    msg.msg_control=control;msg.msg_controllen=sizeof(control);
    const ssize_t count=::recvmsg(fd,&msg,0);
    ::close(fd);
    if(count!=1 || byte!='F' || (msg.msg_flags&MSG_CTRUNC))
        throw std::runtime_error("QMP broker failed to return a peer");
    const auto *c=CMSG_FIRSTHDR(&msg);
    if(!c || c->cmsg_level!=SOL_SOCKET || c->cmsg_type!=SCM_RIGHTS ||
       c->cmsg_len < CMSG_LEN(sizeof(int)))
        throw std::runtime_error("QMP broker returned no valid FD");
    int received=-1;
    std::memcpy(&received,CMSG_DATA(c),sizeof(received));
    if(received<0)throw std::runtime_error("QMP broker returned invalid FD");
    return received;
}
constexpr const char* listener_path="/org/qemu/Display1/Listener";
constexpr const char* listener_iface="org.qemu.Display1.Listener";
constexpr const char* map_iface="org.qemu.Display1.Listener.Unix.Map";
constexpr const char* listener_xml_text=R"xml(
<node>
<interface name="org.qemu.Display1.Listener">
<property name="Interfaces" type="as" access="read"/>
<method name="Scanout"><arg type="u"/><arg type="u"/><arg type="u"/><arg type="u"/><arg type="ay"/></method>
<method name="Update"><arg type="i"/><arg type="i"/><arg type="i"/><arg type="i"/><arg type="u"/><arg type="u"/><arg type="ay"/></method>
<method name="ScanoutDMABUF"><arg type="h"/><arg type="u"/><arg type="u"/><arg type="u"/><arg type="u"/><arg type="t"/><arg type="b"/></method>
<method name="UpdateDMABUF"><arg type="i"/><arg type="i"/><arg type="i"/><arg type="i"/></method>
<method name="Disable"/>
<method name="MouseSet"><arg type="i"/><arg type="i"/><arg type="i"/></method>
<method name="CursorDefine"><arg type="i"/><arg type="i"/><arg type="i"/><arg type="i"/><arg type="ay"/></method>
</interface>
<interface name="org.qemu.Display1.Listener.Unix.Map">
<method name="ScanoutMap"><arg type="h"/><arg type="u"/><arg type="u"/><arg type="u"/><arg type="u"/><arg type="u"/></method>
<method name="UpdateMap"><arg type="i"/><arg type="i"/><arg type="i"/><arg type="i"/></method>
</interface>
</node>
)xml";
constexpr GDBusInterfaceVTable listener_vtable={
    &GioQemuDisplay::method_call,&GioQemuDisplay::get_property,nullptr,{0}
};
std::string gerror(GError* error,const char* fallback) {
    if (!error) return fallback;
    std::string out=error->message;
    g_error_free(error);
    return out;
}
std::span<const std::uint8_t> variant_bytes(GVariant* parameters,std::size_t index,
                                            GVariant** owned) {
    *owned=g_variant_get_child_value(parameters,index);
    gsize size=0;
    const auto* bytes=static_cast<const std::uint8_t*>(
       g_variant_get_fixed_array(*owned,&size,sizeof(std::uint8_t)));
    return {bytes,size};
}
}

GioQemuDisplay::GioQemuDisplay(std::string address,std::string destination,bool enable_audio):
    address_(std::move(address)),destination_(std::move(destination)),enable_audio_(enable_audio),
    console_path_("/org/qemu/Display1/Console_0") {}
GioQemuDisplay::~GioQemuDisplay() {stop();}
void GioQemuDisplay::report(std::string err) noexcept {
    {
       std::lock_guard lock(state_mutex_);
       error_=err;
    }
    if(callbacks_.on_error) callbacks_.on_error(std::move(err));
}
std::string GioQemuDisplay::error() const {
    std::lock_guard lock(state_mutex_);return error_;
}
void GioQemuDisplay::start(qmdp::QemuDisplayCallbacks callbacks) {
    if(!callbacks.on_frame) throw std::invalid_argument("missing QEMU frame callback");
    if(running_.exchange(true)) throw std::logic_error("GIO capture already started");
    callbacks_=std::move(callbacks);
    GError* err=nullptr;
    GDBusConnectionFlags flags=G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT;
    if(!destination_.empty()) flags=static_cast<GDBusConnectionFlags>(
        flags|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION);
    if(address_.starts_with("fd:")||address_.starts_with("broker:")) {
        // The broker gives each Sunshine capture probe its own private peer.
        // GIO performs the standard D-Bus SASL handshake itself.
        int owned=-1;
        if(address_.starts_with("broker:")) {
            owned=receive_broker_fd(address_.substr(7));
        }else{
            const auto fd_text=address_.substr(3);
            std::size_t used=0;
            int fd=-1;
            try{fd=std::stoi(fd_text,&used);}catch(...){running_=false;throw std::invalid_argument("invalid inherited QMP peer FD");}
            if(used!=fd_text.size() || fd<0 || fd>1048575){running_=false;throw std::invalid_argument("invalid inherited QMP FD");}
            owned=fcntl(fd,F_DUPFD_CLOEXEC,3);
        }
        if(owned<0){running_=false;throw std::runtime_error("could not dup QMP peer FD");}
        auto *socket=g_socket_new_from_fd(owned,&err);
        if(!socket){::close(owned);running_=false;throw std::runtime_error(gerror(err,"could not create peer socket"));}
        g_socket_set_timeout(socket,8);
        auto *stream=g_socket_connection_factory_create_connection(socket);
        g_object_unref(socket);
        main_=g_dbus_connection_new_sync(
            G_IO_STREAM(stream),nullptr,
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
            nullptr,nullptr,&err);
        if(main_)g_socket_set_timeout(g_socket_connection_get_socket(stream),0);
        g_object_unref(stream);
    }else{
        main_=g_dbus_connection_new_for_address_sync(
            address_.c_str(),flags,nullptr,nullptr,&err);
    }
    if(!main_) {
        running_=false;
        throw std::runtime_error(gerror(err,"could not connect QEMU D-Bus"));
    }
    // Cache the pointer mode once, then avoid round trips on each mouse event.
    {
      GError* property_error=nullptr;
      g_autoptr(GVariant) reply=g_dbus_connection_call_sync(
         main_,destination_.empty()?nullptr:destination_.c_str(),
         console_path_.c_str(),"org.freedesktop.DBus.Properties","Get",
         g_variant_new("(ss)","org.qemu.Display1.Mouse","IsAbsolute"),
         G_VARIANT_TYPE("(v)"),G_DBUS_CALL_FLAGS_NONE,2000,nullptr,&property_error);
      if(reply) {
        g_autoptr(GVariant) nested=nullptr;
        g_variant_get(reply,"(@v)",&nested);
        g_autoptr(GVariant) value=g_variant_get_variant(nested);
        if(g_variant_is_of_type(value,G_VARIANT_TYPE_BOOLEAN))
           mouse_absolute_=g_variant_get_boolean(value);
      }
      if(property_error)g_error_free(property_error);
    }
    int pair[2]{-1,-1};
    if(::socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)<0) {
       stop();throw std::runtime_error("cannot create QEMU listener socketpair");
    }
    g_autoptr(GUnixFDList) fds=g_unix_fd_list_new();
    int index=g_unix_fd_list_append(fds,pair[1],&err);
    if(index<0) {
      ::close(pair[0]);::close(pair[1]);stop();
      throw std::runtime_error(gerror(err,"cannot attach QEMU listener FD"));
    }
    g_autoptr(GVariant) result=g_dbus_connection_call_with_unix_fd_list_sync(
       main_,destination_.empty()?nullptr:destination_.c_str(),
       console_path_.c_str(),"org.qemu.Display1.Console","RegisterListener",
       g_variant_new("(h)",index),G_VARIANT_TYPE_UNIT,
       G_DBUS_CALL_FLAGS_NONE,8000,fds,nullptr,nullptr,&err);
    ::close(pair[1]);
    if(!result) {
       ::close(pair[0]);stop();
       throw std::runtime_error(gerror(err,"QEMU RegisterListener refused"));
    }
    int listener_fd=pair[0];
    try {
       worker_=std::thread([this,listener_fd]{serve_peer(listener_fd);});
    } catch(...) {
       ::close(listener_fd);stop();throw;
    }
    if (enable_audio_ && callbacks_.on_audio) {
       try {
           audio_=std::make_unique<GioQemuAudio>(main_,destination_,callbacks_.on_audio);
           audio_->start();
       } catch(const std::exception& ex) {
           // QEMU may have no virtual USB sound card. Do not kill video.
           std::fprintf(stderr,"ReiMS audio unavailable: %s\n",ex.what());
           audio_.reset();
       }
    }
}
void GioQemuDisplay::publish(qmdp::FrameToken&& frame) {
    if (!frame.valid()) return;
    width_=frame.surface->width;
    height_=frame.surface->height;
    frame.sequence=++frames_;
    {
      std::lock_guard lock(state_mutex_);
      last_frame_=frame;  // retain immutable pixel storage, no extra copy
    }
    if(running_ && callbacks_.on_frame) callbacks_.on_frame(std::move(frame));
}
void GioQemuDisplay::repaint_cursor() {
    std::optional<qmdp::FrameToken> frame;
    {
      std::lock_guard lock(state_mutex_);
      if(last_frame_) frame=last_frame_;
    }
    if(!frame) return;
    frame->cursor=framebuffer_.cursor_state();
    frame->produced_at=std::chrono::steady_clock::now();
    publish(std::move(*frame));
}
void GioQemuDisplay::method_call(GDBusConnection*,const gchar*,const gchar*,
   const gchar* iface,const gchar* method,GVariant* params,
   GDBusMethodInvocation* invocation,gpointer data) {
    auto *self=static_cast<GioQemuDisplay*>(data);
    self->handle(iface,method,params,invocation);
}
GVariant* GioQemuDisplay::get_property(GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar* name,GError**,gpointer) {
    if(std::strcmp(name,"Interfaces")!=0) return nullptr;
    const gchar* names[]{"org.qemu.Display1.Listener.Unix.Map",nullptr};
    return g_variant_new_strv(names,1);
}
void GioQemuDisplay::listener_closed(GDBusConnection*,gboolean,GError* err,gpointer data) {
    auto *self=static_cast<GioQemuDisplay*>(data);
    if(self->running_) self->report(err?err->message:"QEMU listener disconnected");
    if(self->loop_) g_main_loop_quit(self->loop_);
}
void GioQemuDisplay::handle(const char *iface,const char *method,
  GVariant* params,GDBusMethodInvocation* invoke) {
    try {
       if(!std::strcmp(iface,map_iface)&&!std::strcmp(method,"ScanoutMap")) {
         gint32 handle;guint32 off,w,h,stride,format;
         g_variant_get(params,"(huuuuu)",&handle,&off,&w,&h,&stride,&format);
         auto *fds=g_dbus_message_get_unix_fd_list(g_dbus_method_invocation_get_message(invoke));
         GError* err=nullptr;
         int fd=fds?g_unix_fd_list_get(fds,handle,&err):-1;
         if(fd<0) throw std::runtime_error(gerror(err,"missing scanout FD"));
         publish(framebuffer_.scanout_map(qmdp::UniqueFd(fd),off,w,h,stride,format));
       } else if(!std::strcmp(iface,map_iface)&&!std::strcmp(method,"UpdateMap")) {
         gint32 x,y,w,h;g_variant_get(params,"(iiii)",&x,&y,&w,&h);
         const auto state=framebuffer_.damage_compatibility(x,y,w,h);
         if(state==qmdp::CpuFramebuffer::DamageCompatibility::accepted)
           publish(framebuffer_.update_map(x,y,w,h));
       } else if(!std::strcmp(iface,listener_iface)&&!std::strcmp(method,"Scanout")) {
         guint32 w,h,stride,format;GVariant* arr=nullptr;
         g_variant_get(params,"(uuuu@ay)",&w,&h,&stride,&format,&arr);
         gsize length=0;
         const auto* bytes=static_cast<const std::uint8_t*>(
             g_variant_get_fixed_array(arr,&length,1));
         auto frame=framebuffer_.scanout_inline(w,h,stride,format,{bytes,length});
         g_variant_unref(arr);
         publish(std::move(frame));
       } else if(!std::strcmp(iface,listener_iface)&&!std::strcmp(method,"Update")) {
         gint32 x,y,w,h;guint32 stride,format;GVariant* arr=nullptr;
         g_variant_get(params,"(iiiiuu@ay)",&x,&y,&w,&h,&stride,&format,&arr);
         if(framebuffer_.damage_compatibility(x,y,w,h)==
             qmdp::CpuFramebuffer::DamageCompatibility::accepted) {
            gsize length=0;
            const auto* bytes=static_cast<const std::uint8_t*>(
               g_variant_get_fixed_array(arr,&length,1));
            publish(framebuffer_.update_inline(x,y,w,h,stride,format,{bytes,length}));
         }
         g_variant_unref(arr);
       } else if(!std::strcmp(iface,listener_iface)&&!std::strcmp(method,"MouseSet")) {
          gint32 x,y,on;g_variant_get(params,"(iii)",&x,&y,&on);
          pointer_x_=x;
          pointer_y_=y;
          framebuffer_.set_cursor_position(x,y,on!=0);
          if(callbacks_.on_cursor) callbacks_.on_cursor(framebuffer_.cursor_state());
          repaint_cursor();
       } else if(!std::strcmp(iface,listener_iface)&&!std::strcmp(method,"CursorDefine")) {
          gint32 w,h,hx,hy;GVariant* arr=nullptr;
          g_variant_get(params,"(iiii@ay)",&w,&h,&hx,&hy,&arr);
          gsize count=0;
          const auto* bytes=static_cast<const std::uint8_t*>(
              g_variant_get_fixed_array(arr,&count,1));
          framebuffer_.set_cursor_shape(w,h,hx,hy,{bytes,count});
          g_variant_unref(arr);
          if(callbacks_.on_cursor) callbacks_.on_cursor(framebuffer_.cursor_state());
          repaint_cursor();
       } else if(!std::strcmp(iface,listener_iface)&&!std::strcmp(method,"Disable")) {
          framebuffer_.disable();
       } else {
          g_dbus_method_invocation_return_error(invoke,G_DBUS_ERROR,
              G_DBUS_ERROR_NOT_SUPPORTED,"Unsupported QEMU frame method %s",method);
          return;
       }
       g_dbus_method_invocation_return_value(invoke,g_variant_new("()"));
    } catch(const std::exception& ex) {
       report(std::string("QEMU display callback: ")+ex.what());
       g_dbus_method_invocation_return_error(invoke,G_DBUS_ERROR,
              G_DBUS_ERROR_FAILED,"%s",ex.what());
    }
}
void GioQemuDisplay::serve_peer(int fd) {
    context_=g_main_context_new();
    g_main_context_push_thread_default(context_);
    GError* err=nullptr;
    GSocket* socket=g_socket_new_from_fd(fd,&err);
    if(!socket) {
       report(gerror(err,"could not adopt QEMU peer socket"));
       g_main_context_pop_thread_default(context_);
       g_main_context_unref(context_);context_=nullptr;return;
    }
    g_socket_set_timeout(socket,5);
    GSocketConnection* raw=g_socket_connection_factory_create_connection(socket);
    g_object_unref(socket);
    peer_=g_dbus_connection_new_sync(
        G_IO_STREAM(raw),nullptr,static_cast<GDBusConnectionFlags>(
            G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_DELAY_MESSAGE_PROCESSING),
        nullptr,nullptr,&err);
    if(peer_)g_socket_set_timeout(g_socket_connection_get_socket(raw),0);
    g_object_unref(raw);
    if(!peer_) {
       report(gerror(err,"QEMU GIO authentication failed"));
       g_main_context_pop_thread_default(context_);
       g_main_context_unref(context_);context_=nullptr;return;
    }
    listener_xml_=g_dbus_node_info_new_for_xml(listener_xml_text,&err);
    if(!listener_xml_) {report(gerror(err,"invalid GIO listener XML"));goto cleanup;}
    {
      guint reg1=g_dbus_connection_register_object(peer_,listener_path,
          listener_xml_->interfaces[0],&listener_vtable,this,nullptr,&err);
      if(!reg1) {report(gerror(err,"register QEMU Listener failed"));goto cleanup;}
      guint reg2=g_dbus_connection_register_object(peer_,listener_path,
          listener_xml_->interfaces[1],&listener_vtable,this,nullptr,&err);
      if(!reg2) {report(gerror(err,"register QEMU Map listener failed"));goto cleanup;}
    }
    g_signal_connect(peer_,"closed",G_CALLBACK(&GioQemuDisplay::listener_closed),this);
    loop_=g_main_loop_new(context_,FALSE);
    g_dbus_connection_start_message_processing(peer_);
    {
      auto* cancel_check=g_timeout_source_new(50);
      g_source_set_callback(cancel_check,+[](gpointer opaque)->gboolean {
        auto* self=static_cast<GioQemuDisplay*>(opaque);
        if(!self->running_){g_main_loop_quit(self->loop_);return G_SOURCE_REMOVE;}
        return G_SOURCE_CONTINUE;
      },this,nullptr);
      g_source_attach(cancel_check,context_);
      if(running_)g_main_loop_run(loop_);
      g_source_destroy(cancel_check);g_source_unref(cancel_check);
    }
cleanup:
    if(loop_) {g_main_loop_unref(loop_);loop_=nullptr;}
    if(peer_) {g_dbus_connection_close_sync(peer_,nullptr,nullptr);g_object_unref(peer_);peer_=nullptr;}
    if(listener_xml_) {g_dbus_node_info_unref(listener_xml_);listener_xml_=nullptr;}
    g_main_context_pop_thread_default(context_);
    g_main_context_unref(context_);
    context_=nullptr;
}
void GioQemuDisplay::send_no_reply(const char *iface,const char *method,GVariant *args) {
    if(!main_ || !running_) return;
    GDBusMessage *msg=g_dbus_message_new_method_call(
      destination_.empty()?nullptr:destination_.c_str(),console_path_.c_str(),iface,method);
    g_dbus_message_set_body(msg,args);
    g_dbus_message_set_flags(msg,G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED);
    GError* err=nullptr;
    if(!g_dbus_connection_send_message(main_,msg,G_DBUS_SEND_MESSAGE_FLAGS_NONE,nullptr,&err))
        report(gerror(err,"QEMU input send failed"));
    g_object_unref(msg);
}
void GioQemuDisplay::set_ui_info(const qmdp::ViewportRequest& r) {
    const guint16 wmm=static_cast<guint16>(std::clamp(r.width/4U,1U,65535U));
    const guint16 hmm=static_cast<guint16>(std::clamp(r.height/4U,1U,65535U));
    send_no_reply("org.qemu.Display1.Console","SetUIInfo",
                  g_variant_new("(qqiiuu)",wmm,hmm,0,0,r.width,r.height));
}
void GioQemuDisplay::key(std::uint32_t c,bool down) {
    send_no_reply("org.qemu.Display1.Keyboard",down?"Press":"Release",g_variant_new("(u)",c));
}
void GioQemuDisplay::button(std::uint8_t b,bool down) {
    send_no_reply("org.qemu.Display1.Mouse",down?"Press":"Release",g_variant_new("(u)",static_cast<guint32>(b)));
}
bool GioQemuDisplay::is_absolute_pointer() {return mouse_absolute_.load();}
void GioQemuDisplay::absolute_pointer(std::uint32_t x,std::uint32_t y) {
    const auto xmax=width_.load()?width_.load()-1U:0U;
    const auto ymax=height_.load()?height_.load()-1U:0U;
    x=std::min(x,xmax);y=std::min(y,ymax);
    pointer_x_=static_cast<std::int32_t>(x);
    pointer_y_=static_cast<std::int32_t>(y);
    if(!mouse_absolute_)return;
    send_no_reply("org.qemu.Display1.Mouse","SetAbsPosition",g_variant_new("(uu)",x,y));
}
void GioQemuDisplay::relative_pointer(std::int32_t x,std::int32_t y) {
    if(mouse_absolute_) {
       // QEMU's usb-tablet device rejects RelMotion. Move its absolute
       // pointer by the requested delta to retain desktop functionality.
       const auto boundx=width_.load()?width_.load()-1U:0U;
       const auto boundy=height_.load()?height_.load()-1U:0U;
       auto px=std::clamp<std::int64_t>(
          static_cast<std::int64_t>(pointer_x_.load())+x,0,boundx);
       auto py=std::clamp<std::int64_t>(
          static_cast<std::int64_t>(pointer_y_.load())+y,0,boundy);
       absolute_pointer(static_cast<std::uint32_t>(px),static_cast<std::uint32_t>(py));
       return;
    }
    send_no_reply("org.qemu.Display1.Mouse","RelMotion",g_variant_new("(ii)",x,y));
}
void GioQemuDisplay::stop() noexcept {
    if(!running_.exchange(false)) return;
    if(audio_){audio_->stop();audio_.reset();}
    if(worker_.joinable()) worker_.join();
    if(main_) {g_dbus_connection_close_sync(main_,nullptr,nullptr);g_object_unref(main_);main_=nullptr;}
}
} // namespace reims_stream

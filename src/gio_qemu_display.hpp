#pragma once
#include "capture/cpu_framebuffer.hpp"
#include "gio_qemu_audio.hpp"
#include "relative_pointer_target.hpp"
#include "interfaces/qemu_display.hpp"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace reims_stream {
// Native GIO adapter for QEMU's GLib D-Bus Display1 peer. QSM's systemd
// peer serializer is not wire-compatible with GIO's SASL authentication.
// Reuse QSM's independently tested, bounded CPU framebuffer implementation.
class GioQemuDisplay final: public qmdp::IQemuDisplay {
public:
    GioQemuDisplay(std::string address, std::string destination="org.qemu",bool enable_audio=false);
    ~GioQemuDisplay() override;
    GioQemuDisplay(const GioQemuDisplay&)=delete;
    GioQemuDisplay& operator=(const GioQemuDisplay&)=delete;

    void start(qmdp::QemuDisplayCallbacks callbacks) override;
    void stop() noexcept override;
    void set_ui_info(const qmdp::ViewportRequest& request) override;
    void key(std::uint32_t code, bool pressed) override;
    void button(std::uint8_t button, bool pressed) override;
    bool is_absolute_pointer() override;
    void absolute_pointer(std::uint32_t x, std::uint32_t y) override;
    void relative_pointer(std::int32_t x, std::int32_t y) override;

    [[nodiscard]] std::uint64_t frame_count() const noexcept {return frames_.load();}
    [[nodiscard]] std::string error() const;
    [[nodiscard]] GioQemuAudio::Stats audio_stats() const {return audio_?audio_->stats():GioQemuAudio::Stats{};}
public:
    static void method_call(GDBusConnection*,const gchar*,const gchar*,
      const gchar*,const gchar*,GVariant*,GDBusMethodInvocation*,gpointer);
    static void listener_closed(GDBusConnection*,gboolean,GError*,gpointer);
    static GVariant* get_property(GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GError**,gpointer);
private:
    void handle(const char *interface,const char *method,GVariant *parameters,
                GDBusMethodInvocation *invocation);
    void serve_peer(int fd);
    void send_no_reply(const char *interface,const char *method,GVariant *args);
    void publish(qmdp::FrameToken&& frame);
    void repaint_cursor();
    void report(std::string text) noexcept;

    std::string address_,destination_,console_path_;
    bool enable_audio_{false};
    std::unique_ptr<GioQemuAudio> audio_;
    qmdp::QemuDisplayCallbacks callbacks_;
    qmdp::CpuFramebuffer framebuffer_;
    GDBusConnection *main_{}; // refcounted
    GDBusConnection *peer_{};
    GDBusNodeInfo *listener_xml_{};
    GMainContext *context_{};
    GMainLoop *loop_{};
    std::thread worker_;
    mutable std::mutex state_mutex_;
    std::string error_;
    std::optional<qmdp::FrameToken> last_frame_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<bool> mouse_absolute_{true};
    RelativePointerTarget pointer_target_;
    std::atomic<std::uint32_t> width_{1920},height_{1080};
};
} // namespace reims_stream

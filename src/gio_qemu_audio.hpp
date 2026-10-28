#pragma once
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace reims_stream {
// QEMU's GLib peer-to-peer AudioOutListener; keeps PCM off its video thread.
class GioQemuAudio final {
public:
 using AudioCallback = std::function<void(std::span<const float>,std::uint32_t,std::uint16_t)>;
 explicit GioQemuAudio(GDBusConnection* main,std::string destination,AudioCallback callback);
 ~GioQemuAudio();
 GioQemuAudio(const GioQemuAudio&)=delete;
 GioQemuAudio& operator=(const GioQemuAudio&)=delete;
 void start();
 void stop() noexcept;
 struct Stats {std::uint64_t chunks{},frames{},errors{};bool connected{};};
 [[nodiscard]] Stats stats() const;
private:
 struct Stream {
  std::uint8_t bits{},channels{};
  bool signed_pcm{},float_pcm{},big_endian{},enabled{true},mute{false};
  std::uint32_t frequency{},frame_bytes{};
  std::vector<std::uint8_t> volume;
 };
public:
 static void method_call(GDBusConnection*,const gchar*,const gchar*,const gchar*,const gchar*,GVariant*,GDBusMethodInvocation*,gpointer);
 static void closed(GDBusConnection*,gboolean,GError*,gpointer);
private:
 void handle(const char*,GVariant*,GDBusMethodInvocation*);
 void worker_loop(int fd);
 void on_write(std::uint64_t id,std::span<const std::uint8_t> data);
 GDBusConnection *main_{};  // borrowed; owner must stop audio before main
 std::string destination_;
 AudioCallback callback_;
 GDBusConnection* peer_{};
 GDBusNodeInfo* xml_{};
 GMainContext* context_{};
 GMainLoop* loop_{};
 std::thread worker_;
 mutable std::mutex mutex_;
 std::unordered_map<std::uint64_t,Stream> streams_;
 std::atomic<bool> running_{false},connected_{false};
 std::atomic<std::uint64_t> chunks_{0},frames_{0},errors_{0};
};
} // namespace reims_stream

#pragma once
#include "audio_queue.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <span>
#include <thread>

namespace reims_stream {
// Optional audio output: QEMU D-Bus PCM -> bounded latency buffer -> a
// dedicated PulseAudio/PipeWire null sink monitored by Sunshine. No host speakers.
class PulseAudioBridge {
public:
 PulseAudioBridge() = default;
 ~PulseAudioBridge();
 PulseAudioBridge(const PulseAudioBridge&)=delete;
 PulseAudioBridge& operator=(const PulseAudioBridge&)=delete;
 void start();
 void stop() noexcept;
 void submit(std::span<const float> pcm, std::uint32_t rate, std::uint16_t channels);
 struct Stats {AudioQueue::Stats queue; std::uint64_t connection_errors{},playback_errors{},played_chunks{};};
 Stats stats() const;
private:
 void worker() noexcept;
 AudioQueue queue_{std::chrono::milliseconds{120}};
 std::mutex mutex_;
 std::condition_variable cv_;
 std::thread worker_;
 std::atomic<bool> active_{false};
 std::atomic<std::uint64_t> connection_errors_{0},playback_errors_{0},played_chunks_{0};
};
} // namespace reims_stream

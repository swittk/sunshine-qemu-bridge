#pragma once
#include "core/frame.hpp"
#include "core/latest_frame_mailbox.hpp"
#include "interfaces/qemu_display.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>

namespace reims_stream {

enum class CaptureStatus {
    frame,
    timeout,
    resize,
    interrupted,
    error
};

struct FrameMetadata {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
    std::chrono::steady_clock::time_point produced_at{};
};

struct CaptureStatistics {
    qmdp::LatestFrameMailbox::Stats mailbox;
    std::uint64_t conversion_errors{};
    std::uint64_t connection_errors{};
    std::string latest_error;
};

// Receives QEMU's D-Bus frames without queuing stale video. No direct
// dependence on Sunshine's C++ headers: Sunshine uses its own img_t buffers
// and passes them as an output span to next_bgra().
class CaptureFeed {
public:
    explicit CaptureFeed(qmdp::IQemuDisplay& source);
    ~CaptureFeed();
    CaptureFeed(const CaptureFeed&) = delete;
    CaptureFeed& operator=(const CaptureFeed&) = delete;

    void start();
    void stop() noexcept;
    std::optional<FrameMetadata> wait_geometry(std::chrono::milliseconds timeout);
    CaptureStatus next_bgra(
        std::span<std::uint8_t> destination,
        std::uint32_t expected_width,
        std::uint32_t expected_height,
        std::chrono::milliseconds timeout,
        FrameMetadata& output);

    [[nodiscard]] CaptureStatistics stats() const;
private:
    qmdp::IQemuDisplay& source_;
    qmdp::LatestFrameMailbox frames_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> conversion_errors_{0};
    std::atomic<std::uint64_t> connection_errors_{0};
    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_;
    std::optional<FrameMetadata> latest_geometry_;
    std::string latest_error_;
};

} // namespace reims_stream

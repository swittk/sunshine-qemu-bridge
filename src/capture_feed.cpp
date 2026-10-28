#include "capture_feed.hpp"
#include "cursor_blend.hpp"
#include "core/pixel_format.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace reims_stream {
namespace {

bool write_bgra(const qmdp::FrameToken& frame, std::span<std::uint8_t> dst) {
    if (!frame.valid() || !frame.surface || !frame.surface->cpu()) {
        return false;
    }
    const auto& surface = *frame.surface;
    const auto& pixels = *surface.cpu();
    const auto width = static_cast<std::size_t>(surface.width);
    const auto height = static_cast<std::size_t>(surface.height);
    const auto stride = static_cast<std::size_t>(pixels.stride);
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        width > std::numeric_limits<std::size_t>::max() / 4U ||
        height > std::numeric_limits<std::size_t>::max() / (width * 4U)) {
        return false;
    }
    const auto row_bytes = width * 4U;
    const auto total_bytes = row_bytes * height;
    if (stride < row_bytes || stride > std::numeric_limits<std::size_t>::max() / height ||
        !pixels.bytes || pixels.bytes->size() < stride * height ||
        dst.size() < total_bytes) {
        return false;
    }
    const auto format = qmdp::pixman_layout(pixels.pixman_format);
    if (format == qmdp::PackedPixelLayout::unsupported) {
        return false;
    }
    for (std::size_t y = 0; y < height; ++y) {
        const auto *src = pixels.bytes->data() + y * stride;
        auto *target = dst.data() + (surface.y0_top ? y : height - 1U - y) * row_bytes;
        if (format == qmdp::PackedPixelLayout::bgra) {
            std::memcpy(target, src, row_bytes);
            continue;
        }
        for (std::size_t x = 0; x < width; ++x) {
            const auto *p = src + x * 4U;
            auto *d = target + x * 4U;
            switch (format) {
                case qmdp::PackedPixelLayout::bgrx:
                    d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d[3] = 255; break;
                case qmdp::PackedPixelLayout::rgba:
                    d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = p[3]; break;
                case qmdp::PackedPixelLayout::rgbx:
                    d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = 255; break;
                case qmdp::PackedPixelLayout::argb:
                    d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0]; break;
                case qmdp::PackedPixelLayout::xrgb:
                    d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = 255; break;
                case qmdp::PackedPixelLayout::abgr:
                    d[0] = p[1]; d[1] = p[2]; d[2] = p[3]; d[3] = p[0]; break;
                case qmdp::PackedPixelLayout::xbgr:
                    d[0] = p[1]; d[1] = p[2]; d[2] = p[3]; d[3] = 255; break;
                case qmdp::PackedPixelLayout::bgra:
                case qmdp::PackedPixelLayout::unsupported:
                    return false;
            }
        }
    }
    return true;
}

} // namespace

CaptureFeed::CaptureFeed(qmdp::IQemuDisplay& source): source_(source) {}
CaptureFeed::~CaptureFeed() { stop(); }
void CaptureFeed::set_audio_callback(AudioCallback callback) {
  if (running_) throw std::logic_error("set audio callback before start");
  audio_callback_=std::move(callback);
}

void CaptureFeed::start() {
    if (running_.exchange(true)) {
        throw std::logic_error("capture already started");
    }
    try {
        qmdp::QemuDisplayCallbacks callbacks;
        callbacks.on_frame = [this](qmdp::FrameToken frame) {
            if (!running_) return;
            if (!frame.valid()) return;
            {
                std::lock_guard lock(state_mutex_);
                latest_geometry_ = FrameMetadata{
                    frame.surface->width, frame.surface->height,
                    frame.sequence, frame.produced_at
                };
            }
            state_cv_.notify_all();
            (void) frames_.publish(std::move(frame));
        };
        callbacks.on_audio = [this](std::span<const float> samples,std::uint32_t rate,std::uint16_t channels){
            if(running_ && audio_callback_) audio_callback_(samples,rate,channels);
        };
        callbacks.on_error = [this](std::string error) {
            {
                std::lock_guard lock(state_mutex_);
                latest_error_ = std::move(error);
                connection_errors_.fetch_add(1, std::memory_order_relaxed);
            }
            state_cv_.notify_all();
        };
        source_.start(std::move(callbacks));
    } catch (...) {
        running_ = false;
        source_.stop();
        frames_.close();
        state_cv_.notify_all();
        throw;
    }
}

void CaptureFeed::stop() noexcept {
    if (!running_.exchange(false)) return;
    source_.stop();
    frames_.close();
    state_cv_.notify_all();
}

std::optional<FrameMetadata> CaptureFeed::wait_geometry(std::chrono::milliseconds timeout) {
    std::unique_lock lock(state_mutex_);
    state_cv_.wait_for(lock, timeout, [this] {
        return latest_geometry_.has_value() || !running_ || !latest_error_.empty();
    });
    return latest_geometry_;
}

CaptureStatus CaptureFeed::next_bgra(
    std::span<std::uint8_t> destination,
    std::uint32_t expected_width,
    std::uint32_t expected_height,
    std::chrono::milliseconds timeout,
    FrameMetadata& output) {
    if (!running_) return CaptureStatus::interrupted;
    auto frame = frames_.wait_pop(timeout);
    if (!frame) {
        if (!running_) return CaptureStatus::interrupted;
        std::lock_guard lock(state_mutex_);
        return latest_error_.empty() ? CaptureStatus::timeout : CaptureStatus::error;
    }
    output = FrameMetadata{
        frame->surface->width, frame->surface->height,
        frame->sequence, frame->produced_at
    };
    if (output.width != expected_width || output.height != expected_height) {
        return CaptureStatus::resize;
    }
    if (!write_bgra(*frame, destination)) {
        conversion_errors_.fetch_add(1, std::memory_order_relaxed);
        return CaptureStatus::error;
    }
    blend_cursor_bgra(frame->cursor,destination,output.width,output.height);
    return CaptureStatus::frame;
}

CaptureStatistics CaptureFeed::stats() const {
    std::lock_guard lock(state_mutex_);
    return {
        frames_.stats(),
        conversion_errors_.load(std::memory_order_relaxed),
        connection_errors_.load(std::memory_order_relaxed),
        latest_error_
    };
}

} // namespace reims_stream

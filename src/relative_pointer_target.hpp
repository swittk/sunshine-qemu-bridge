#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <utility>

namespace reims_stream {
// A QEMU absolute tablet may report coordinates rounded down by one pixel.
// Relative motion must integrate against our requested position, never the
// quantized/delayed MouseSet feedback from QEMU, or both axes drift per packet.
class RelativePointerTarget {
public:
    void reported(std::int32_t x, std::int32_t y) {
        std::lock_guard guard(mutex_);
        if (!seeded_) {
            x_ = x;
            y_ = y;
            seeded_ = true;
        }
    }

    std::pair<std::uint32_t, std::uint32_t> absolute(
        std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
        std::lock_guard guard(mutex_);
        x_ = static_cast<std::int32_t>(std::min(x, width ? width - 1 : 0U));
        y_ = static_cast<std::int32_t>(std::min(y, height ? height - 1 : 0U));
        seeded_ = true;
        return {static_cast<std::uint32_t>(x_), static_cast<std::uint32_t>(y_)};
    }

    std::pair<std::uint32_t, std::uint32_t> relative(
        std::int32_t dx, std::int32_t dy, std::uint32_t width, std::uint32_t height) {
        std::lock_guard guard(mutex_);
        if (!seeded_) {
            x_ = static_cast<std::int32_t>(width / 2);
            y_ = static_cast<std::int32_t>(height / 2);
            seeded_ = true;
        }
        x_ = static_cast<std::int32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(x_) + dx, 0, width ? width - 1 : 0U));
        y_ = static_cast<std::int32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(y_) + dy, 0, height ? height - 1 : 0U));
        return {static_cast<std::uint32_t>(x_), static_cast<std::uint32_t>(y_)};
    }

private:
    std::mutex mutex_;
    bool seeded_ = false;
    std::int32_t x_{}, y_{};
};
} // namespace reims_stream

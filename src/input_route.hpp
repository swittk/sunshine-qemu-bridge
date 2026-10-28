#pragma once
#include "interfaces/qemu_display.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
namespace reims_stream {
class InputRoute {
public:
    static InputRoute& instance();
    void attach(const std::shared_ptr<qmdp::IQemuDisplay>& display);
    void detach(const qmdp::IQemuDisplay* display);
    bool key(std::uint16_t virtual_key, bool pressed, bool extended = false) noexcept;
    bool relative_mouse(std::int32_t dx, std::int32_t dy) noexcept;
    bool absolute_mouse(std::int32_t x, std::int32_t y) noexcept;
    bool button(std::uint8_t button, bool pressed) noexcept;
    bool wheel(std::int32_t distance, bool horizontal = false) noexcept;
    [[nodiscard]] bool connected() const;
private:
    std::shared_ptr<qmdp::IQemuDisplay> display() const;
    mutable std::mutex mutex_;
    std::weak_ptr<qmdp::IQemuDisplay> display_;
    std::set<std::uint32_t> pressed_keys_;
    std::set<std::uint8_t> pressed_buttons_;
    std::int64_t wheel_remainder_{};
    void release_locked(const std::shared_ptr<qmdp::IQemuDisplay>& display) noexcept;
};
std::optional<std::uint32_t> vk_to_qemu(std::uint16_t virtual_key, bool extended = false) noexcept;
} // namespace reims_stream

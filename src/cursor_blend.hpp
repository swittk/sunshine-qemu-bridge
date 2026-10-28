#pragma once
#include "core/frame.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
namespace reims_stream {
// Composite QEMU's 32-bit ARGB cursor over a top-origin BGRA frame.
// Unsafe or malformed metadata is ignored rather than propagating to encoder.
void blend_cursor_bgra(
    const qmdp::CursorState& cursor,
    std::span<std::uint8_t> frame,
    std::uint32_t width,
    std::uint32_t height) noexcept;
} // namespace reims_stream

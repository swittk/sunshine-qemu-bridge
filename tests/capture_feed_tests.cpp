#include "capture_feed.hpp"
#include "core/pixel_format.hpp"

#include <array>
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using reims_stream::CaptureFeed;
using reims_stream::CaptureStatus;
using reims_stream::FrameMetadata;

class FakeDisplay final: public qmdp::IQemuDisplay {
public:
    void start(qmdp::QemuDisplayCallbacks cb) override { callback_ = std::move(cb); }
    void stop() noexcept override { callback_ = {}; }
    void set_ui_info(const qmdp::ViewportRequest&) override {}
    void key(std::uint32_t, bool) override {}
    void button(std::uint8_t, bool) override {}
    bool is_absolute_pointer() override { return true; }
    void absolute_pointer(std::uint32_t, std::uint32_t) override {}
    void relative_pointer(std::int32_t, std::int32_t) override {}
    void send(qmdp::FrameToken token) { callback_.on_frame(std::move(token)); }
    void error(const char *message) { callback_.on_error(message); }
private:
    qmdp::QemuDisplayCallbacks callback_;
};

qmdp::FrameToken frame(
    std::uint32_t width, std::uint32_t height, std::uint32_t stride,
    std::uint32_t pixman, std::vector<std::uint8_t> data,
    std::uint64_t seq = 1, bool top = true) {
    auto surface = std::make_shared<qmdp::FrameSurface>();
    surface->width = width;
    surface->height = height;
    surface->y0_top = top;
    surface->generation = 1;
    surface->storage = qmdp::CpuPixels{
        .stride = stride,
        .pixman_format = pixman,
        .bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(data))
    };
    qmdp::FrameToken out;
    out.surface = std::move(surface);
    out.sequence = seq;
    out.produced_at = std::chrono::steady_clock::now();
    out.damage = {0U, 0U, width, height};
    return out;
}

void checks() {
    FakeDisplay source;
    CaptureFeed feed(source);
    feed.start();
    // One 2x1 BGRA frame, including source stride padding.
    source.send(frame(2,1,12,qmdp::pixman_a8r8g8b8,
        {1,2,3,4, 5,6,7,8, 99,99,99,99}));
    auto geom = feed.wait_geometry(10ms);
    assert(geom && geom->width == 2 && geom->height == 1);
    std::array<std::uint8_t,8> bgra{};
    FrameMetadata meta;
    auto status = feed.next_bgra(bgra,2,1,10ms,meta);
    assert(status == CaptureStatus::frame && meta.sequence == 1);
    assert((bgra == std::array<std::uint8_t,8>{1,2,3,4,5,6,7,8}));

    source.send(frame(2,1,8,qmdp::pixman_x8r8g8b8,{11,12,13,0,21,22,23,0},2));
    assert(feed.next_bgra(bgra,2,1,10ms,meta)==CaptureStatus::frame);
    assert((bgra == std::array<std::uint8_t,8>{11,12,13,255,21,22,23,255}));

    source.send(frame(2,1,8,qmdp::pixman_a8b8g8r8,{10,20,30,40,41,51,61,71},3));
    assert(feed.next_bgra(bgra,2,1,10ms,meta)==CaptureStatus::frame);
    assert((bgra == std::array<std::uint8_t,8>{30,20,10,40,61,51,41,71}));

    // y0_top=false: normalize scanout into top-origin BGRA.
    source.send(frame(1,2,4,qmdp::pixman_a8r8g8b8,{1,2,3,4,5,6,7,8},4,false));
    assert(feed.next_bgra(bgra,1,2,10ms,meta)==CaptureStatus::frame);
    assert((bgra == std::array<std::uint8_t,8>{5,6,7,8,1,2,3,4}));

    // Resize must never copy into an image allocated for the old dimensions.
    source.send(frame(4,1,16,qmdp::pixman_a8r8g8b8,std::vector<std::uint8_t>(16,77),5));
    assert(feed.next_bgra(bgra,2,1,10ms,meta)==CaptureStatus::resize);
    assert(meta.width == 4 && meta.height == 1);

    // An unsupported format and undersized storage fail closed.
    source.send(frame(2,1,8,0,std::vector<std::uint8_t>(8,1),6));
    assert(feed.next_bgra(bgra,2,1,10ms,meta)==CaptureStatus::error);
    source.send(frame(2,1,8,qmdp::pixman_a8r8g8b8,std::vector<std::uint8_t>(4,1),7));
    assert(feed.next_bgra(bgra,2,1,10ms,meta)==CaptureStatus::error);

    // Slow encoder: one-slot mailbox must discard stale input.
    for (std::uint64_t n=8; n<80; ++n) {
        source.send(frame(2,1,8,qmdp::pixman_a8r8g8b8,std::vector<std::uint8_t>(8,(std::uint8_t)n),n));
    }
    assert(feed.next_bgra(bgra,2,1,10ms,meta)==CaptureStatus::frame);
    assert(meta.sequence==79 && bgra[0]==79);
    assert(feed.stats().mailbox.dropped >= 70);
    assert(feed.next_bgra(bgra,2,1,2ms,meta)==CaptureStatus::timeout);
    source.error("fake disconnect");
    assert(feed.next_bgra(bgra,2,1,2ms,meta)==CaptureStatus::error);
    assert(feed.stats().connection_errors == 1);
    feed.stop();
    assert(feed.next_bgra(bgra,2,1,1ms,meta)==CaptureStatus::interrupted);
}

int main() {
    checks();
    std::puts("capture feed: frame format, stride, resize, error, drop, shutdown PASS");
}

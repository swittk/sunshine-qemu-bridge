#pragma once
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace video { struct config_t {}; }
namespace platf {
enum class mem_type_e { unknown, cuda, vaapi };
enum class pix_fmt_e { nv12 };
enum class capture_e { ok, reinit, timeout, interrupted, capture_error };
struct img_t {
    std::uint8_t *data{};
    int width{}, height{}, pixel_pitch{}, row_pitch{};
    std::optional<std::chrono::steady_clock::time_point> frame_timestamp;
    virtual ~img_t() = default;
};
struct avcodec_encode_device_t {virtual ~avcodec_encode_device_t()=default;};
struct display_t {
    using push_captured_image_cb_t=std::function<bool(std::shared_ptr<img_t>&&,bool)>;
    using pull_free_image_cb_t=std::function<bool(std::shared_ptr<img_t>&)>;
    virtual ~display_t()=default;
    virtual capture_e capture(const push_captured_image_cb_t&,const pull_free_image_cb_t&,bool*)=0;
    virtual std::shared_ptr<img_t> alloc_img()=0;
    virtual int dummy_img(img_t*)=0;
    virtual std::unique_ptr<avcodec_encode_device_t> make_avcodec_encode_device(pix_fmt_e) {return nullptr;}
    int width{},height{},logical_width{},logical_height{},env_width{},env_height{},env_logical_width{},env_logical_height{};
};
}

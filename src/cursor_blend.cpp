#include "cursor_blend.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
namespace reims_stream {
void blend_cursor_bgra(const qmdp::CursorState& cursor,
    std::span<std::uint8_t> pixels,std::uint32_t w,std::uint32_t h) noexcept {
    if(!cursor.visible || !cursor.shape || w==0 || h==0)return;
    const auto &shape=*cursor.shape;
    if(shape.width==0 || shape.height==0 ||
       shape.width>1024 || shape.height>1024 ||
       shape.hotspot_x>=shape.width || shape.hotspot_y>=shape.height ||
       pixels.size()<static_cast<std::uint64_t>(w)*h*4ULL ||
       shape.argb.size()<static_cast<std::uint64_t>(shape.width)*shape.height*4ULL)return;
    const auto left=static_cast<std::int64_t>(cursor.x)-shape.hotspot_x;
    const auto top=static_cast<std::int64_t>(cursor.y)-shape.hotspot_y;
    for(std::uint32_t y=0;y<shape.height;++y) {
       const auto dy=top+static_cast<std::int64_t>(y);
       if(dy<0||dy>=static_cast<std::int64_t>(h))continue;
       for(std::uint32_t x=0;x<shape.width;++x) {
         const auto dx=left+static_cast<std::int64_t>(x);
         if(dx<0||dx>=static_cast<std::int64_t>(w))continue;
         const auto so=(static_cast<std::size_t>(y)*shape.width+x)*4;
         // Host-native little-endian ARGB8888 is stored B,G,R,A.
         const std::uint32_t alpha=shape.argb[so+3];
         if(alpha==0)continue;
         const auto dest=(static_cast<std::size_t>(dy)*w+
                         static_cast<std::size_t>(dx))*4;
         for(int c=0;c<3;++c) {
           const std::uint32_t fg=shape.argb[so+c];
           const std::uint32_t bg=pixels[dest+c];
           pixels[dest+c]=static_cast<std::uint8_t>(
                 (fg*alpha+bg*(255-alpha)+127U)/255U);
         }
         pixels[dest+3]=255;
       }
    }
}
} // namespace reims_stream

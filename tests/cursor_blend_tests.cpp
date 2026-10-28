#undef NDEBUG
#include "cursor_blend.hpp"
#include <array>
#include <cassert>
#include <memory>
#include <vector>
#include <cstdio>
int main(){
  qmdp::CursorState cursor{};
  auto shape=std::make_shared<qmdp::CursorShape>();
  shape->width=2;shape->height=2;
  shape->hotspot_x=1;shape->hotspot_y=1;
  shape->argb={255,0,0,255, 0,255,0,0, 0,0,255,128, 5,10,15,255};
  cursor.visible=true;cursor.x=2;cursor.y=2;cursor.shape=shape;
  std::vector<std::uint8_t> data(4U*4U*4U,0U);
  for(size_t i=3;i<data.size();i+=4)data[i]=255;
  reims_stream::blend_cursor_bgra(cursor,data,4,4);
  const auto idx=[&](size_t x,size_t y){return (y*4U+x)*4U;};
  assert(data[idx(1,1)]==255 && data[idx(1,1)+3]==255);
  assert(data[idx(2,1)]==0 && data[idx(2,1)+1]==0); // transparent
  assert(data[idx(1,2)+2]==128); // alpha blended red
  assert(data[idx(2,2)]==5 && data[idx(2,2)+1]==10);
  cursor.visible=false;
  std::vector<std::uint8_t> original=data;
  reims_stream::blend_cursor_bgra(cursor,data,4,4);
  assert(data==original);
  cursor.visible=true;cursor.x=0;cursor.y=0; // clipped by top/left
  std::vector<std::uint8_t> edge(4U*4U*4U,0);
  reims_stream::blend_cursor_bgra(cursor,edge,4,4);
  assert(edge[0]==5 && edge[1]==10);
  // Malformed shape is ignored without touching the framebuffer.
  shape->argb.resize(3);
  original=edge;
  reims_stream::blend_cursor_bgra(cursor,edge,4,4);
  assert(edge==original);
  std::puts("Cursor alpha, hotspot, clipping, hidden, malformed PASS");
}

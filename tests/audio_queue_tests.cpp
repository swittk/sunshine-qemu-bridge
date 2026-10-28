#undef NDEBUG
#include "audio_queue.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
int main(){
  reims_stream::AudioQueue q(std::chrono::milliseconds{30});
  std::vector<float> chunk(960,0.5f); // 10 ms 48k stereo
  assert(!q.push(chunk,0,2));
  assert(!q.push(chunk,48000,0));
  assert(q.push(chunk,48000,2));
  assert(q.push(chunk,48000,2));
  assert(q.push(chunk,48000,2));
  assert(q.push(chunk,48000,2));
  assert(q.push(chunk,48000,2));
  const auto stats=q.stats();
  assert(stats.published==5 && stats.dropped>=2 && stats.pending<=3);
  auto f=q.pop();assert(f && f->sample_rate==48000 && f->channels==2);
  std::size_t count=1;while(q.pop())++count;
  assert(count<=3);
  assert(q.stats().pending==0);
  std::vector<float> tiny(2,0.2f);
  for(int i=0;i<10000;++i)assert(q.push(tiny,48000,2));
  assert(q.stats().pending<=256 && q.stats().dropped>9000);
  std::vector<float> too_long(48000,0.2f);
  assert(!q.push(too_long,48000,2));
  std::puts("Audio queue: timing, tiny-chunk metadata bound and oversized rejection PASS");
}

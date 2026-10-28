#undef NDEBUG
#include <cassert>
#include "input_route.hpp"
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>
#include <string>
#include <tuple>

class ProbeDisplay final: public qmdp::IQemuDisplay {
public:
 std::vector<std::pair<std::uint32_t,bool>> keys;
 std::vector<std::pair<std::uint8_t,bool>> buttons;
 std::vector<std::pair<std::int32_t,std::int32_t>> rel;
 std::vector<std::pair<std::uint32_t,std::uint32_t>> abs;
 void start(qmdp::QemuDisplayCallbacks) override {}
 void stop() noexcept override {}
 void set_ui_info(const qmdp::ViewportRequest&) override {}
 void key(std::uint32_t value,bool down) override {keys.emplace_back(value,down);}
 void button(std::uint8_t value,bool down) override {buttons.emplace_back(value,down);}
 bool is_absolute_pointer() override {return true;}
 void absolute_pointer(std::uint32_t x,std::uint32_t y) override {abs.emplace_back(x,y);}
 void relative_pointer(std::int32_t x,std::int32_t y) override {rel.emplace_back(x,y);}
};
int main() {
 using reims_stream::InputRoute;
 auto &r=InputRoute::instance();
 assert(!r.connected());
 assert(!r.key(0x41,true));
 auto d=std::make_shared<ProbeDisplay>();
 r.attach(d);
 assert(r.connected());
 assert(r.key(0x41,true));
 assert(r.key(0x41,false));
 assert(d->keys.size()==2 && d->keys[0]==std::make_pair(30U,true));
 assert(d->keys[1]==std::make_pair(30U,false));
 assert(r.key(0x11,true,true) && d->keys[2].first==0x9dU);
 assert(r.key(0x5b,true) && d->keys[3].first==0xdbU);
 assert(r.key(0x70,true) && d->keys[4].first==59U);
 assert(r.key(0x27,true) && d->keys[5].first==0xcdU);
 assert(!r.key(0xff,true));
 assert(d->keys.size()==6);
 assert(r.relative_mouse(17,-8) && d->rel.back()==std::make_pair(17,-8));
 assert(r.absolute_mouse(-5,100) && d->abs.back()==std::make_pair(0U,100U));
 assert(r.button(0,true) && r.button(0,false));
 assert(d->buttons.size()==2 && d->buttons[0]==std::make_pair((std::uint8_t)0,true));
 assert(r.wheel(240));
 assert(d->buttons.size()==6);
 assert(d->buttons[2].first==3 && d->buttons[3].first==3);
 assert(!r.button(8,true));
 assert(r.button(5,true) && r.button(5,false)); // QEMU side
 assert(r.button(6,true) && r.button(6,false)); // QEMU extra
 assert(!r.button(3,true)); // wheel is not a held button
 assert(!r.wheel(120,true)); // horizontal scrolling not supported
 r.detach(d.get());
 assert(!r.connected());
 assert(!r.key(0x41,true));
 auto d2=std::make_shared<ProbeDisplay>();
 r.attach(d2);
 assert(r.key(0x7a,true) && d2->keys.back().first==87);
 assert(r.key(0x7b,true) && d2->keys.back().first==88);
 assert(r.button(0,true));
 auto wheel_start=d2->buttons.size();
 assert(r.wheel(60));assert(d2->buttons.size()==wheel_start);
 assert(r.wheel(60));assert(d2->buttons.size()==wheel_start+2);
 assert(r.wheel(INT32_MIN)); // must not overflow abs(INT_MIN)
 r.detach(d2.get());
 assert(d2->keys.size()==4 && !d2->keys.back().second);
 assert(d2->buttons.back()==std::make_pair((std::uint8_t)0,false));
 assert(!r.connected());
 std::puts("QEMU input mapping, held-key release, fractional wheel and INT_MIN PASS");
}

#include "input_route.hpp"
#include <algorithm>
#include <array>
#include <utility>
namespace reims_stream {
namespace {
constexpr std::array<std::uint8_t,26> letters = {
  30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44
};
constexpr std::array<std::uint8_t,10> digits = {11,2,3,4,5,6,7,8,9,10};
}
std::optional<std::uint32_t> vk_to_qemu(std::uint16_t code, bool ext) noexcept {
 if (code >= 0x41 && code <= 0x5A) return letters[code - 0x41];
 if (code >= 0x30 && code <= 0x39) return digits[code - 0x30];
 if (code >= 0x70 && code <= 0x79) return 59U + code - 0x70;
 if (code >= 0x60 && code <= 0x69) {
    constexpr std::array<std::uint8_t,10> numpad = {82,79,80,81,75,76,77,71,72,73};
    return numpad[code - 0x60];
 }
 switch (code) {
 case 0x7A: return 87; // F11
 case 0x7B: return 88; // F12
 case 0x08: return 14; // backspace
 case 0x09: return 15; // tab
 case 0x0D: return ext ? 0x9c : 28; // enter
 case 0x10: return ext ? 54 : 42; // shift
 case 0xA0: return 42; case 0xA1: return 54;
 case 0x11: return ext ? 0x9d : 29; // ctrl
 case 0xA2: return 29; case 0xA3: return 0x9d;
 case 0x12: return ext ? 0xb8 : 56; // alt
 case 0xA4: return 56; case 0xA5: return 0xb8;
 case 0x5B: return 0xdb; case 0x5C: return 0xdc;
 case 0x1B: return 1; // escape
 case 0x20: return 57; // space
 case 0x14: return 58; // caps
 case 0x90: return 69; case 0x91: return 70;
 case 0x21: return 0xc9; case 0x22: return 0xd1;
 case 0x23: return 0xcf; case 0x24: return 0xc7;
 case 0x25: return 0xcb; case 0x26: return 0xc8;
 case 0x27: return 0xcd; case 0x28: return 0xd0;
 case 0x2D: return 0xd2; case 0x2E: return 0xd3;
 case 0x2C: return 0xb7; // printscreen
 case 0x5D: return 0xdd; // context menu
 case 0x6A: return 55; case 0x6B: return 78;
 case 0x6D: return 74; case 0x6E: return 83;
 case 0x6F: return 0xb5;
 case 0x6C: return 0x9c;
 case 0xBA: return 39; // ;:
 case 0xBB: return 13; // =+
 case 0xBC: return 51; // ,
 case 0xBD: return 12; // -
 case 0xBE: return 52; // .
 case 0xBF: return 53; // /?
 case 0xC0: return 41; // ~
 case 0xDB: return 26; // [
 case 0xDC: return 43; // backslash
 case 0xDD: return 27; // ]
 case 0xDE: return 40; // '
 case 0xE2: return 86; // intl backslash
 default: return std::nullopt; // never guess or synthesize unknown keys
 }
}
InputRoute& InputRoute::instance() {
 static InputRoute route;
 return route;
}
void InputRoute::release_locked(const std::shared_ptr<qmdp::IQemuDisplay>& dev) noexcept {
 if(dev) {
   for(auto key:pressed_keys_)try{dev->key(key,false);}catch(...){}
   for(auto button:pressed_buttons_)try{dev->button(button,false);}catch(...){}
 }
 pressed_keys_.clear();pressed_buttons_.clear();wheel_remainder_=0;
}
void InputRoute::attach(const std::shared_ptr<qmdp::IQemuDisplay>& src) {
 std::lock_guard lock(mutex_);
 if(auto old=display_.lock();old && old!=src)release_locked(old);
 display_ = src;
}
void InputRoute::detach(const qmdp::IQemuDisplay* source) {
 std::lock_guard lock(mutex_);
 auto current=display_.lock();
 if (current && current.get() != source) return;
 release_locked(current);
 display_.reset();
}
std::shared_ptr<qmdp::IQemuDisplay> InputRoute::display() const {
 std::lock_guard lock(mutex_);
 return display_.lock();
}
bool InputRoute::connected() const { return !!display(); }
bool InputRoute::key(std::uint16_t vk, bool pressed, bool extended) noexcept {
 const auto code=vk_to_qemu(vk,extended);
 std::lock_guard lock(mutex_);
 auto dev=display_.lock();
 if (!code || !dev) return false;
 try {
   dev->key(*code,pressed);
   if(pressed)pressed_keys_.insert(*code);else pressed_keys_.erase(*code);
   return true;
 } catch (...) {return false;}
}
bool InputRoute::relative_mouse(std::int32_t dx, std::int32_t dy) noexcept {
 auto dev=display(); if (!dev) return false;
 try {dev->relative_pointer(dx,dy);return true;} catch (...) {return false;}
}
bool InputRoute::absolute_mouse(std::int32_t x, std::int32_t y) noexcept {
 auto dev=display(); if (!dev) return false;
 try {dev->absolute_pointer(static_cast<std::uint32_t>(std::max(0,x)),
 static_cast<std::uint32_t>(std::max(0,y)));return true;} catch (...) {return false;}
}
bool InputRoute::button(std::uint8_t button, bool pressed) noexcept {
 std::lock_guard lock(mutex_);
 auto dev=display_.lock(); if (!dev) return false;
 if (button > 6 || button==3 || button==4) return false;
 try {
   dev->button(button,pressed);
   if(pressed)pressed_buttons_.insert(button);else pressed_buttons_.erase(button);
   return true;
 } catch (...) {return false;}
}
bool InputRoute::wheel(std::int32_t distance, bool horizontal) noexcept {
 std::lock_guard lock(mutex_);
 auto dev=display_.lock(); if (!dev || distance==0 || horizontal) return false;
 // Preserve fractional wheel deltas; 5/6 are side buttons, not horizontal wheel.
 const std::int64_t total=wheel_remainder_+static_cast<std::int64_t>(distance);
 wheel_remainder_=total%120;
 const auto button = static_cast<std::uint8_t>(total>0?3:4);
 const auto magnitude=total<0?-total:total;
 const auto steps=std::min<std::int64_t>(magnitude/120,20);
 try { for(int i=0;i<steps;++i){dev->button(button,true);dev->button(button,false);} return true; }
 catch (...) {return false;}
}
} // namespace reims_stream

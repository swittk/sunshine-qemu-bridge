#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>
namespace reims_stream {
struct AudioChunk {
 std::vector<float> samples;
 std::uint32_t sample_rate{};
 std::uint16_t channels{};
 std::chrono::steady_clock::time_point timestamp{};
};
class AudioQueue {
public:
 explicit AudioQueue(std::chrono::milliseconds limit=std::chrono::milliseconds{120}):
   limit_ns_(std::chrono::duration_cast<std::chrono::nanoseconds>(limit).count()) {
   if(limit_ns_<=0)throw std::invalid_argument("audio latency limit must be positive");
 }
 bool push(std::span<const float> source,std::uint32_t sample_rate,std::uint16_t channels) {
    if(channels<1 || channels>8 || sample_rate<8000 || sample_rate>192000 ||
       source.empty() || source.size()>sample_rate*channels/4U || source.size()%channels) return false;
    const auto duration=duration_ns(source.size(),sample_rate,channels);
    if(duration>limit_ns_)return false;
    auto chunk=AudioChunk{std::vector<float>(source.begin(),source.end()),sample_rate,channels,std::chrono::steady_clock::now()};
    std::lock_guard lock(mutex_);
    queue_.emplace_back(std::move(chunk));
    buffered_ns_+=duration;
    ++published_;
    // Also bound metadata overhead for tiny (<1ms) chunks. Integer millisecond
    // rounding must not turn these chunks into an unlimited zero-duration queue.
    while(queue_.size()>1 && (buffered_ns_>limit_ns_ || queue_.size()>256)) {
       const auto& old=queue_.front();
       buffered_ns_-=duration_ns(old.samples.size(),old.sample_rate,old.channels);
       queue_.pop_front();++dropped_;
    }
    return true;
 }
 std::optional<AudioChunk> pop() {
    std::lock_guard lock(mutex_);
    if(queue_.empty()) return std::nullopt;
    auto out=std::move(queue_.front());queue_.pop_front();
    buffered_ns_-=duration_ns(out.samples.size(),out.sample_rate,out.channels);
    ++consumed_;return out;
 }
 struct Stats {std::uint64_t published{},consumed{},dropped{};std::size_t pending{};};
 Stats stats() const {
   std::lock_guard lock(mutex_);return {published_,consumed_,dropped_,queue_.size()};
 }
private:
 static std::int64_t duration_ns(std::size_t samples,std::uint32_t rate,std::uint16_t channels) {
   return static_cast<std::int64_t>((static_cast<std::uint64_t>(samples)*1000000000ULL)/(rate*channels));
 }
 std::int64_t limit_ns_,buffered_ns_{};
 mutable std::mutex mutex_;
 std::deque<AudioChunk> queue_;
 std::uint64_t published_{},consumed_{},dropped_{};
};
}

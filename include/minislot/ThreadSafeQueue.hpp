#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace minislot {

// Classic producer-consumer queue. The socket-receive thread pushes
// incoming frames; a worker thread pops and processes them, so a slow
// handler never blocks the receive loop and a burst of packets queues up
// instead of being dropped at the socket.
template <typename T>
class ThreadSafeQueue {
 public:
  void push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(item));
    condVar_.notify_one();
  }

  // Blocks until an item is available or stop() is called. Returns
  // std::nullopt only when stopped and empty - the consumer's signal to exit.
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    condVar_.wait(lock, [this] { return !queue_.empty() || stopped_; });
    if (queue_.empty()) return std::nullopt;
    T item = std::move(queue_.front());
    queue_.pop_front();
    return item;
  }

  void stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    condVar_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable condVar_;
  std::deque<T> queue_;
  bool stopped_ = false;
};

}  // namespace minislot

/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_EXECUTOR_H_
#define MINICOMPS_EXECUTOR_H_

#include <minicomps/any_aligned_storage.h>

#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <chrono>
#include <atomic>

namespace mc {

/// A work queue.
class executor {
public:
  executor() {}

  template<typename CallbackType, typename DataType>
  void enqueue_work(CallbackType&& item, DataType&& data) {
    if (!mutex_.try_lock()) {
      ++num_lock_failures_;
      mutex_.lock();
    }

    task& work_item = work_items_.emplace_back();
    work_item.fun = std::move(item);
    work_item.data = std::move(data);
    mutex_.unlock();
  }

  void execute() {
    if (!mutex_.try_lock()) {
      ++num_lock_failures_;
      mutex_.lock();
    }

    std::swap(work_items_, work_items_back_buffer_);
    mutex_.unlock();

    for (auto& item : work_items_back_buffer_)
      item.execute();

    work_items_back_buffer_.clear();
  }

  static int num_lock_failures() {
    return num_lock_failures_;
  }

private:
  static std::atomic_int num_lock_failures_;

  struct task {
    any_aligned_storage<64> data;
    std::function<void(void*)> fun;

    void execute() {
      fun(data.get_aligned_ptr());
    }
  };

  std::vector<task> work_items_;
  std::vector<task> work_items_back_buffer_;
  std::chrono::steady_clock::time_point last_execute_;
  std::mutex mutex_;
};

using executor_ptr = std::shared_ptr<executor>;

}

#endif // MINICOMPS_EXECUTOR_H_

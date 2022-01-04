/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_EXECUTOR_H_
#define MINICOMPS_EXECUTOR_H_

#include <minicomps/any_aligned_storage.h>

#include <vector>
#include <functional>
#include <memory>
#include <mutex>

namespace mc {

/// A work queue.
class executor {
public:
  template<typename CallbackType, typename DataType>
  void enqueue_work(CallbackType&& item, DataType&& data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    task& work_item = work_items_.emplace_back();
    work_item.fun = std::move(item);
    work_item.data = std::move(data);
  }

  void execute() {
    {
      std::lock_guard<std::recursive_mutex> lock(mutex_);
      std::swap(work_items_, work_items_back_buffer_);
    }

    for (auto& item : work_items_back_buffer_)
      item.execute();

    work_items_back_buffer_.clear();
  }

private:
  struct task {
    any_aligned_storage<64> data;
    std::function<void(void*)> fun;

    void execute() {
      fun(data.get_aligned_ptr());
    }
  };

  std::vector<task> work_items_;
  std::vector<task> work_items_back_buffer_;
  std::recursive_mutex mutex_;
};

using executor_ptr = std::shared_ptr<executor>;

}

#endif // MINICOMPS_EXECUTOR_H_

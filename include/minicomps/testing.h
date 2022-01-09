/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_TESTING_H
#define MINICOMPS_TESTING_H

#include <minicomps/executor.h>

#include <memory>
#include <vector>
#include <iostream>
#include <chrono>

namespace mc {

class component_registry {
public:
  template<typename ComponentType, typename... ArgumentTypes>
  std::shared_ptr<ComponentType> create(ArgumentTypes&&... args) {
    auto component = std::make_shared<ComponentType>(std::forward<ArgumentTypes>(args)...);
    component->publish();
    return component;
  }

  component_registry() = default;
  component_registry(const component_registry&) = delete;
  component_registry& operator =(const component_registry&) = delete;

  ~component_registry() {
    for (auto& component : components_) {
      component->unpublish();
    }
  }

private:
  std::vector<std::shared_ptr<component>> components_;
};

template<typename CallbackType>
int measure(CallbackType&& callback) {
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  callback();
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  int duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  std::cout << "Duration: " << duration << " ms" << std::endl;
  return duration;
}

template<typename CallbackType>
int measure_with_allocs(CallbackType&& callback) {
  testing::alloc_counter counter;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  int executor_lock_failures_start = executor::num_lock_failures();

  callback();
  std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
  int duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  std::cout << "Duration: " << duration << " ms, " << counter.total_allocation_count() << " allocs, " << executor::num_lock_failures() - executor_lock_failures_start << " executor lock failures" << std::endl;
  return duration;
}

}

#endif // MINICOMPS_TESTING_H

/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_ANY_ALIGNED_STORAGE_H_
#define MINICOMPS_ANY_ALIGNED_STORAGE_H_

#include <cstddef>
#include <functional>
#include <memory>

namespace mc {

/// Like a combination of std::any and std::aligned_storage; ie, the type is specified when
/// writing the data rather than when declaring the object. Types aren't verified, so the client
/// needs to know that the cast is OK.
template<std::size_t Length>
class any_aligned_storage {
public:
  any_aligned_storage() {}
  any_aligned_storage(any_aligned_storage<Length>&& other) {*this = std::move(other); }
  ~any_aligned_storage() {destroy(); }

  any_aligned_storage& operator =(any_aligned_storage<Length>&& other) {
    destroy();

    if (!other.aligned_ptr_)
      return *this;

    aligned_ptr_ = storage_ + other.aligned_diff(); // Diff is OK since the storage is max-aligned
    other.move_construct_(aligned_ptr_, other.aligned_ptr_);
    other.destroy();

    deleter_ = std::move(other.deleter_); // These aren't deleted by destroy, so is fine
    move_construct_ = std::move(other.move_construct_);

    return *this;
  }

  template<typename T>
  any_aligned_storage& operator =(T&& value) {
    destroy();

    std::size_t used_storage = 0;
    aligned_ptr_ = storage_;

    std::align(alignof(T), sizeof(T), aligned_ptr_, used_storage);
    new (aligned_ptr_) T(std::move(value));

    deleter_ = [](void* ptr) {
      check_alignment<T>(ptr);
      static_cast<T*>(ptr)->~T();
    };

    move_construct_ = [](void* target, void* source) {
      check_alignment<T>(target);
      check_alignment<T>(source);
      new (target) T(std::move(*static_cast<T*>(source)));
    };

    return *this;
  }

  template<typename T>
  const T* get() const {
    if (!aligned_ptr_)
      return nullptr;

    check_alignment<T>(aligned_ptr_);
    return static_cast<T*>(aligned_ptr_);
  }

  void* get_aligned_ptr() const {
    return aligned_ptr_;
  }

private:
  void destroy() {
    if (!aligned_ptr_)
      return;

    deleter_(aligned_ptr_);
    aligned_ptr_ = nullptr;
  }

  ptrdiff_t aligned_diff() const {
    return static_cast<const char*>(aligned_ptr_) - storage_;
  }

  template<typename T>
  static void check_alignment(void* ptr) {
    if (reinterpret_cast<uintptr_t>(ptr) % alignof(T) != 0)
      std::abort();
  }

  alignas(alignof(std::max_align_t)) char storage_[Length];
  void* aligned_ptr_ = nullptr;
  std::function<void(void*)> deleter_;
  std::function<void(void*, void*)> move_construct_;
};

}

#endif // MINICOMPS_ANY_ALIGNED_STORAGE_H_

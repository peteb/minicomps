/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_ANY_ALIGNED_STORAGE_H_
#define MINICOMPS_ANY_ALIGNED_STORAGE_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <cassert>

namespace mc {

/// Like a combination of std::any and std::aligned_storage; ie, the type is specified when
/// writing the data rather than when declaring the object. Types aren't verified, so the client
/// needs to know that the cast is OK.
template<std::size_t Length>
class any_aligned_storage {
public:
  any_aligned_storage() {}
  any_aligned_storage(any_aligned_storage<Length>&& other) {assign(std::move(other)); }
  ~any_aligned_storage() {destroy(); }

  any_aligned_storage& assign(any_aligned_storage<Length>&& other) {
    destroy();

    if (other.aligned_ptr_)
      aligned_ptr_ = storage_ + other.aligned_diff(); // Diff is OK since the storage is max-aligned
    else
      aligned_ptr_ = nullptr;

    object_ptr_ = other.move_construct_(aligned_ptr_, other.object_ptr_);

    other.destroy();

    destruct_ = std::move(other.destruct_); // These aren't deleted by destroy, so is fine
    move_construct_ = std::move(other.move_construct_);

    return *this;
  }

  template<typename T>
  any_aligned_storage& assign(T&& value) {
    destroy();

    if constexpr(sizeof(T) + alignof(T) - 1 <= sizeof(storage_)) {
      // We know for sure that there's space for the object and its alignment

      {
        void* aligned_ptr = storage_;
        std::size_t used_storage = sizeof(storage_);
        void* aligned = std::align(alignof(T), sizeof(T), aligned_ptr, used_storage);
        assert(aligned && "aligned object goes out of bounds");
        aligned_ptr_ = aligned_ptr;
      }

      object_ptr_ = new (aligned_ptr_) T(std::move(value));

      destruct_ = [](void* ptr) {
        check_alignment<T>(ptr);
        static_cast<T*>(ptr)->~T();
      };

      move_construct_ = [](void* target, void* source) {
        check_alignment<T>(target); // TODO: can be removed
        check_alignment<T>(source);
        return new (target) T(std::move(*static_cast<T*>(source)));
      };
    }
    else {
      // We might not have enough space for the object, use heap allocation
      // TODO: check if we really need heap allocation
      aligned_ptr_ = nullptr;
      object_ptr_ = new T(std::move(value));

      destruct_ = [](void* ptr) {
        check_alignment<T>(ptr);
        delete static_cast<T*>(ptr);
      };

      move_construct_ = [](void* target, void* source) {
        (void)target; // Not used -- we don't new in-place
        check_alignment<T>(source);
        return new T(std::move(*static_cast<T*>(source)));
      };
    }

    return *this;
  }

  template<typename T>
  const T* get() const {
    if (!object_ptr_)
      return nullptr;

    return static_cast<T*>(object_ptr_);
  }

  void* get_object_ptr() const {
    return object_ptr_;
  }

private:
  void destroy() {
    if (!object_ptr_)
      return;

    destruct_(object_ptr_);
    object_ptr_ = nullptr;
    aligned_ptr_ = nullptr;
  }

  ptrdiff_t aligned_diff() const {
    // Don't call this function if aligned_ptr_ is null
    return static_cast<const char*>(aligned_ptr_) - storage_;
  }

  template<typename T>
  static void check_alignment(void* ptr) {
    if (reinterpret_cast<uintptr_t>(ptr) % alignof(T) != 0)
      std::abort();
  }

  alignas(alignof(std::max_align_t)) char storage_[Length];
  void* aligned_ptr_ = nullptr;
  void* object_ptr_ = nullptr;

  std::function<void(void*)> destruct_;
  std::function<void*(void*, void*)> move_construct_;
};

}

#endif // MINICOMPS_ANY_ALIGNED_STORAGE_H_

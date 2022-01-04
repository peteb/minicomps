/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_MESSAGING_H_
#define MINICOMPS_MESSAGING_H_

#include <cstdint>
#include <functional>
#include <minicomps/executor.h>

namespace mc {

using message_id = uintptr_t;

class message {
public:
  virtual ~message() = default;
};

template<typename MessageType>
message_id get_message_id() {
  static int uniq;
  return message_id{reinterpret_cast<uintptr_t>(&uniq)};
}

template<typename T>
struct signature_util;

template<typename R>
class callback_result;

/// Converts a query signature of R(Args...) to various representations.
template<typename R, typename ...Args>
struct signature_util<R(Args...)> {
  using callback_signature = void(Args..., std::function<void(mc::concrete_result<R>&&)>&&);
  using callback_inner_signature = void(Args..., callback_result<R>&&);

  using return_type = R;
};

class message_handler {
public:
  virtual ~message_handler() = default;
  virtual void* get_handler_ptr() = 0;
};

template<typename Signature>
class message_handler_impl : public message_handler {
public:
  message_handler_impl() = default;
  message_handler_impl(std::function<Signature>&& handler) : handler(std::move(handler)) {}

  virtual void* get_handler_ptr() override {
    return &handler;
  }

  std::function<Signature> handler;
};

template<typename Signature>
class message_handler_async_impl : public message_handler {
public:
  using converted_signature = typename signature_util<Signature>::callback_inner_signature;

  message_handler_async_impl() = default;

  template<typename T>
  message_handler_async_impl(T&& handler) : handler(std::move(handler)) {}

  virtual void* get_handler_ptr() override {
    return &handler;
  }

  std::function<converted_signature> handler;
};


template<typename MessageType>
class query_info {};

}

#define DECLARE_QUERY(name, type)                                                           \
  class name;                                                                               \
  template<> class query_info<name> {                                                       \
  public:                                                                                   \
    using handler_wrapper_type = message_handler_impl<type>;                                \
    using handler_type = decltype(handler_wrapper_type{}.handler);                          \
    using async_handler_wrapper_type = message_handler_async_impl<type>;                    \
    using async_handler_type = decltype(async_handler_wrapper_type{}.handler);              \
    using signature = type;                                                                 \
  };


#endif // MINICOMPS_MESSAGING_H_

/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_MESSAGING_H_
#define MINICOMPS_MESSAGING_H_

#include <minicomps/executor.h>
#include <minicoros/continuation_chain.h>
#include <minicoros/types.h>
#include <minicoros/coroutine.h> // TODO: make it so we don't need this dependency

#include <cstdint>
#include <functional>
#include <utility>

namespace mc {

using message_id = uintptr_t;

class message {
public:
  virtual ~message() = default;
};

template<typename MessageType>
message_id get_message_id() {
  return get_message_id(static_cast<MessageType*>(nullptr));
}

class message_info {
public:
  const char* name;
  message_id id;
};

template<typename T>
struct signature_util;

template<typename R>
class callback_result;

/// Converts a query signature of R(Args...) to various representations.
template<typename R, typename... Args>
struct signature_util<R(Args...)> {
  using callback_signature = void(Args..., std::function<void(mc::concrete_result<R>&&)>&&);
  using callback_inner_signature = void(Args..., callback_result<R>&&);
  using coroutine_type = mc::coroutine<R>;

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

template<typename EventType>
class message_handler_event_impl : public message_handler {
public:
  message_handler_event_impl() = default;

  template<typename T>
  message_handler_event_impl(T&& handler) : handler(std::move(handler)) {}

  virtual void* get_handler_ptr() override {
    return &handler;
  }

  std::function<void(const EventType&)> handler;
};

template<typename MessageType>
using query_info = decltype(get_query_info(std::declval<MessageType>()));

template<typename MessageType>
const message_info& get_message_info() {
  return get_message_info(static_cast<MessageType*>(nullptr));
}

}

#define MINICOMPS_XSTR(s) MINICOMPS_STR(s)
#define MINICOMPS_STR(s) #s

#define MESSAGE_API  // dllexport/dllimport

#define MESSAGE_DECLARATION(name)                                                           \
  MESSAGE_API mc::message_id get_message_id(name*);                                         \
  MESSAGE_API const mc::message_info& get_message_info(name*);

#define MESSAGE_DEFINITION(name)                                                            \
  mc::message_id get_message_id(name*) {                                                    \
    static int uniq;                                                                        \
    return mc::message_id{reinterpret_cast<uintptr_t>(&uniq)};                              \
  }                                                                                         \
  const mc::message_info& get_message_info(name* ptr) {                                     \
    static mc::message_info msg{MINICOMPS_STR(name), get_message_id(ptr)};                            \
    return msg;                                                                             \
  }                                                                                         \

#define DECLARE_QUERY(name, type)                                                           \
  class name {};                                                                            \
  class query_info_##name {                                                                 \
  public:                                                                                   \
    using handler_wrapper_type = message_handler_impl<type>;                                \
    using handler_type = decltype(handler_wrapper_type{}.handler);                          \
    using async_handler_wrapper_type = message_handler_async_impl<type>;                    \
    using async_handler_type = decltype(async_handler_wrapper_type{}.handler);              \
    using signature = type;                                                                 \
  };                                                                                        \
  query_info_##name get_query_info(name);                                                   \
  MESSAGE_DECLARATION(name)

#define DEFINE_QUERY(name) MESSAGE_DEFINITION(name)

#define DECLARE_EVENT(name, type)                                                           \
  struct name type;                                                                         \
  MESSAGE_DECLARATION(name)

#define DEFINE_EVENT(name) MESSAGE_DEFINITION(name)

#define DECLARE_INTERFACE(name) class name; MESSAGE_DECLARATION(name)
#define DECLARE_INTERFACE2(name, contents) struct name contents; MESSAGE_DECLARATION(name)  // Use overloading instead

#define DEFINE_INTERFACE(name) MESSAGE_DEFINITION(name)

#endif // MINICOMPS_MESSAGING_H_

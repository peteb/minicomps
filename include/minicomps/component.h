/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_COMPONENT_H_
#define MINICOMPS_COMPONENT_H_

#include <minicomps/executor.h>
#include <minicomps/lifetime.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace mc {

using message_id = uintptr_t;
class message_info;
class component;

enum class message_type {
  REQUEST,
  RESPONSE,
  LOCKED_REQUEST,
  LOCKED_RESPONSE,
  EVENT
};

class component_listener {
public:
  virtual ~component_listener() = default;

  virtual void on_enqueue(const component* sender, const component* receiver, const message_info& info, message_type type) {}
  virtual void on_invoke(const component* sender, const component* receiver, const message_info& info, message_type type) {}
};

struct dependency_info {
  enum {
    EXPORT,
    IMPORT
  } direction;

  enum {
    ASYNC_POLY,
    INTERFACE,
    GROUP
  } type;

  const message_info& msg_info;

  std::vector<component*> resolved_targets;
};

///
class component {
public:
  component(const std::string& name, executor_ptr executor) : name(name), default_executor(executor) {}
  virtual ~component() = default;

  virtual void publish_dependencies() {}
  virtual void unpublish_dependencies() {}

  virtual void* lookup_async_handler(message_id msg_id) = 0;
  virtual void* lookup_interface(message_id msg_id) = 0;
  virtual executor_ptr lookup_executor_override(message_id msg_id) = 0;
  virtual std::vector<dependency_info> describe_dependencies() = 0;

  const std::string name;                /// Class name of the component's implementation
  const executor_ptr default_executor;   /// Handles incoming async requests and responses
  lifetime default_lifetime;
  component_listener* listener = nullptr;
  bool allow_direct_call_async = true;   /// Whether components are allowed to elide enqueuing their async queries for this component
  bool allow_locking_calls_sync = true;  /// Whether components are allowed to lock this component when calling it synchronously. If false, trying to lock will raise an error

  std::recursive_mutex lock;             /// The component-level lock, used for synchronous queries across threads
};

void set_current_component(component*);
component* get_current_component();
void set_current_lifetime(lifetime_weak_ptr);
lifetime_weak_ptr get_current_lifetime();

}

#endif // MINICOMPS_COMPONENT_H_

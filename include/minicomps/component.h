/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_COMPONENT_H_
#define MINICOMPS_COMPONENT_H_

#include <minicomps/executor.h>
#include <minicomps/lifetime.h>

#include <cstdint>
#include <mutex>
#include <string>

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

/// Components talk to other components by sending messages through a Broker.
/// A component follows this lifecycle:
///   1. construction (constructor gets called)
///       - Inject object dependencies at this time
///       - Other components might not exist
///   2. linking (onLink function gets called)
///       - All other components exist at this point and have been constructed
///       - This is where you can save references to other components
///       - Publish queries and events
///   3. unlinking (onUnlink)
///       - Hard references to other components must be reset here to avoid memory leaks when shutting down
///       - Queries and events are unpublished here
///   4. destruction (destructor)
///       -
class component {
public:
  component(const std::string& name, executor_ptr executor) : name(name), default_executor(executor), listener(nullptr) {}

  virtual ~component() = default;
  virtual void publish() {}
  virtual void unpublish() {}
  virtual void* lookup_sync_handler(message_id msg_id) = 0;
  virtual void* lookup_async_handler(message_id msg_id) = 0;
  virtual executor_ptr lookup_executor_override(message_id msg_id) = 0;

  const std::string name;               /// Class name of the component's implementation
  const executor_ptr default_executor;  /// Handles incoming async requests and responses
  component_listener* listener;
  lifetime default_lifetime;

  std::recursive_mutex lock;    /// The component-level lock, used for synchronous queries across threads of execution
};

}

#endif // MINICOMPS_COMPONENT_H_

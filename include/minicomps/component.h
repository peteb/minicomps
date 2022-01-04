/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_COMPONENT_H_
#define MINICOMPS_COMPONENT_H_

#include <minicomps/executor.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace mc {

using message_id = uintptr_t;

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
  component(const std::string& name, executor_ptr executor) : name(name), executor(executor) {executor_id = executor.get(); }

  virtual ~component() = default;
  virtual void publish() {}
  virtual void unpublish() {}
  virtual void* lookup_sync_handler(message_id msgId) = 0;
  virtual void* lookup_async_handler(message_id msgId) = 0;

  const std::string name;       /// Class name of the component's implementation
  const executor_ptr executor;  /// Handles incoming async requests and responses
  const void* executor_id;      /// For checking whether two components are on the same executor, and thus, same thread of execution

  std::recursive_mutex lock;    /// The component-level lock, used for synchronous queries across threads of execution
};

}

#endif // MINICOMPS_COMPONENT_H_

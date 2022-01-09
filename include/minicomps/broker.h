/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_BROKER_H_
#define MINICOMPS_BROKER_H_

#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace mc {
using message_id = uintptr_t;
class component;
}

namespace mc {
using message_receivers = std::vector<std::weak_ptr<component>>;

/// A broker facilitates communication between components. It knows:
///   - Which component listens to what message type
///   - Which component is interested in this information
class broker {
public:
  void associate(message_id, std::weak_ptr<component>);
  void disassociate(message_id, component*);
  void invalidate(message_id);
  void disassociate_everything(component*);

  /// Returns an immutable list of weak_ptrs to all the components that are
  /// associated with this message at the time of calling the function. Creating/removing
  /// associations will expire the weak_ptr and a new call to `lookup` is necessary.
  std::weak_ptr<message_receivers> lookup(message_id);

private:
  std::unordered_map<message_id, std::shared_ptr<message_receivers>> active_lookups_;
  std::recursive_mutex lookup_mutex_;
};
} // mc

#endif // MINICOMPS_BROKER_H_

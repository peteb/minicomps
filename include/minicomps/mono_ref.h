/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_MONO_REF_H_
#define MINICOMPS_MONO_REF_H_

#include <minicomps/component.h>
#include <minicomps/messaging.h>

#include <tuple>
#include <memory>

namespace mc {

class mono_ref {
public:
  virtual ~mono_ref() = default;
  virtual void reset() = 0;
  virtual void force_resolve() = 0;
  virtual dependency_info create_dependency_info() const = 0;
};

/// References a component's handler code for a specific MessageType. Expects only one
/// handler to exist.
/// Caches as much as possible of all indirections. Relies on the broker to tell us
/// when a message handler has changed by expiring `receivers_`.
///
/// For good performance, we cannot increment/decrement any ref counts on function invocation.
template<typename MessageType, typename HandlerType, typename SubclassType>
class mono_ref_base : public mono_ref {
  HandlerType* handler_ = nullptr;
  std::weak_ptr<message_receivers> receivers_;
  bool same_executor_ = false;
  std::shared_ptr<component> receiver_;
  std::shared_ptr<executor> receiver_executor_;

public:
  mono_ref_base(broker& broker, component& component) : broker_(broker), component_(component) {}

  using handler_type = HandlerType;

  HandlerType* lookup() {
    // Check if we already have valid state for this handler or if we need to refetch
    if (handler_ && !receivers_.expired())
      return handler_;

    const message_id msg_id = get_message_id<MessageType>();

    // Find all components that are associated for this message id
    receivers_ = broker_.lookup(msg_id);
    auto receivers = receivers_.lock();

    if (!receivers) // No receivers
      return nullptr;

    if (receivers->size() != 1) // Too many receivers -- we expect 1
      return nullptr;

    receiver_ = (*receivers)[0].lock();

    if (!receiver_) // Failed to lock; this might happen in races
      return nullptr;

    SubclassType& subclass = *static_cast<SubclassType*>(this);
    handler_ = static_cast<HandlerType*>(subclass.lookup_handler(*receiver_.get(), msg_id));

    if (!handler_) // Receiver possibly out of sync with the broker
      return nullptr;

    if (executor_ptr executor_override = receiver_->lookup_executor_override(msg_id))
      receiver_executor_ = std::move(executor_override);
    else
      receiver_executor_ = receiver_->default_executor;

    // Save whether we're on the same executor. Useful for some optimizations (lock and queue elision)
    same_executor_ = component_.default_executor.get() == receiver_executor_.get();

    return handler_;
  }

  virtual void reset() override {
    handler_ = nullptr;
    receivers_.reset();
    receiver_.reset();
    receiver_executor_.reset();
  }

  virtual void force_resolve() override {
    (void)lookup();
  }

  virtual dependency_info create_dependency_info() const override {
    // TODO: when we move to c++20, change struct initialization to use named field init

    return {
      dependency_info::IMPORT,
      dependency_info::ASYNC_MONO, // TODO: override per async/sync
      get_message_info<MessageType>(),
      {receiver_.get()}
    };
  }

  template<typename... ArgumentTypes>
  auto invoke(ArgumentTypes&&... arguments) -> decltype((*handler_)(std::forward<ArgumentTypes>(arguments)...)) {
    if (!handler_)
      std::abort(); // TODO: customize
    return (*handler_)(std::forward<ArgumentTypes>(arguments)...); // TODO: void
  }

  bool mutual_executor() const {
    return same_executor_;
  }

  executor_ptr& receiver_executor() {
    return receiver_executor_;
  }

  std::shared_ptr<component>& receiver() {
    return receiver_;
  }

private:
  broker& broker_;
  component& component_;
};

// Type aliases to make the types a bit shorter

template<typename MessageType, typename SubclassType>
using sync_mono_ref_base = mono_ref_base<MessageType, typename query_info<MessageType>::handler_type, SubclassType>;

template<typename MessageType, typename SubclassType>
using async_mono_ref_base = mono_ref_base<MessageType, typename query_info<MessageType>::async_handler_type, SubclassType>;

//

template<typename MessageType>
class sync_mono_ref : public sync_mono_ref_base<MessageType, sync_mono_ref<MessageType>> {
public:
  sync_mono_ref(broker& broker, component& component) : sync_mono_ref_base<MessageType, sync_mono_ref<MessageType>>(broker, component) {}

  void* lookup_handler(component& comp, message_id msg_id) {
    return comp.lookup_sync_handler(msg_id);
  }
};

template<typename MessageType>
class async_mono_ref : public async_mono_ref_base<MessageType, async_mono_ref<MessageType>> {
public:
  async_mono_ref(broker& broker, component& component) : async_mono_ref_base<MessageType, async_mono_ref<MessageType>>(broker, component) {}

  void* lookup_handler(component& comp, message_id msg_id) {
    return comp.lookup_async_handler(msg_id);
  }
};

}

#endif // MINICOMPS_MONO_REF_H_

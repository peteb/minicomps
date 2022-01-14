/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_POLY_REF_H_
#define MINICOMPS_POLY_REF_H_

#include <minicomps/component.h>
#include <minicomps/messaging.h>

#include <tuple>
#include <memory>

namespace mc {

// TODO: merge with mono_ref
class poly_ref {
public:
  virtual ~poly_ref() = default;
  virtual void reset() = 0;
  virtual void force_resolve() = 0;
  virtual dependency_info create_dependency_info() const = 0;
};

template<typename MessageType>
class poly_ref_base : public poly_ref {
public:
  poly_ref_base(broker& broker, component& component) : broker_(broker), component_(component) {}

  using handler_type = decltype(message_handler_event_impl<MessageType>{}.handler);

  class receiver_handler {
    handler_type* handler_ = nullptr;

  public:
    receiver_handler(std::shared_ptr<component>&& component, handler_type* handler, bool same_executor)
      : receiving_component_(std::move(component))
      , handler_(handler)
      , same_executor_(same_executor)
      {}

    const std::shared_ptr<component>& receiver() const {
      return receiving_component_;
    }

    bool mutual_executor() const {
      return same_executor_;
    }

    void invoke(const MessageType& argument) {
      if (!handler_)
        std::abort(); // TODO: customize
      (*handler_)(argument);
    }

    handler_type* handler() {
      return handler_;
    }

  private:
    std::shared_ptr<component> receiving_component_;
    bool same_executor_ = false;
  };

  std::vector<receiver_handler>& lookup() {
    // Check if we already have valid state for this handler or if we need to refetch
    if (!receiver_handlers_.empty() && !receivers_.expired())
      return receiver_handlers_;

    const message_id msgId = get_message_id<MessageType>();

    // Find all components that are associated for this message id
    receivers_ = broker_.lookup(msgId);
    auto receivers = receivers_.lock();

    receiver_handlers_.clear();

    if (!receivers) // No receivers
      return receiver_handlers_;

    for (auto& r : *receivers) {
      auto receiver = r.lock();
      if (!receiver) // Failed to lock; this might happen in races
        continue;

      handler_type* handler = static_cast<handler_type*>(receiver->lookup_async_handler(msgId));
      if (!handler) // Receiver possibly out of sync with the broker
        continue;

      // Save whether we're on the same executor. Useful for some optimizations (lock and queue elision)
      bool same_executor = receiver->default_executor.get() == component_.default_executor.get();

      receiver_handlers_.emplace_back(std::move(receiver), handler, same_executor);
    }

    return receiver_handlers_;
  }

  virtual void reset() override {
    receivers_.reset();
    receiver_handlers_.clear();
  }

  virtual void force_resolve() override {
    (void)lookup();
  }

  virtual dependency_info create_dependency_info() const override {
    // TODO: when we move to c++20, change struct initialization to use named field init

    dependency_info info = {
      dependency_info::EXPORT,
      dependency_info::ASYNC_POLY,
      get_message_info<MessageType>()
    };

    for (const receiver_handler& handler : receiver_handlers_) {
      info.resolved_targets.push_back(handler.receiver().get());
    }

    return info;
  }

private:
  broker& broker_;
  component& component_;

  std::weak_ptr<message_receivers> receivers_;
  std::vector<receiver_handler> receiver_handlers_;
};

}

#endif // MINICOMPS_POLY_REF_H_

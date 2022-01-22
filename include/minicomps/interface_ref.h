/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_INTERFACE_REF_H_
#define MINICOMPS_INTERFACE_REF_H_

#include <minicomps/component.h>
#include <minicomps/broker.h>
#include <minicomps/messaging.h>

namespace mc {

class interface_ref {
public:
  virtual ~interface_ref() = default;

  virtual void reset() = 0;
  virtual void force_resolve() = 0;
  virtual dependency_info create_dependency_info() const = 0;
};

template<typename InterfaceType>
class interface_ref_base : public interface_ref {
public:
  interface_ref_base(broker& broker, component& owning_component) : broker_(broker), owning_component_(owning_component) {}

  InterfaceType* lookup() {
    // None of the resolved receivers have changed, so the cached interface pointer should still be good
    if (local_interface_proxy_ && !resolved_receivers_.expired())
      return local_interface_proxy_.get();

    const message_id msg_id = get_message_id<InterfaceType>();
    resolved_receivers_ = broker_.lookup(msg_id);

    auto receivers = resolved_receivers_.lock();

    if (!receivers) // No receivers
      return nullptr;

    if (receivers->size() != 1) // Too many receivers -- we expect 1
      return nullptr;

    receiver_ = (*receivers)[0].lock();

    if (!receiver_) // Failed to lock; this might happen in races
      return nullptr;

    InterfaceType* handler_interface = static_cast<InterfaceType*>(receiver_->lookup_interface(msg_id));
    if (!handler_interface)
      return nullptr;

    // Create a proxy by calling the copy constructor. Yes, this is a bit magical/hacky, but it makes for some nice usage syntax.
    set_current_component(&owning_component_);
    local_interface_proxy_ = std::make_unique<InterfaceType>(*handler_interface);
    set_current_component(nullptr);

    return local_interface_proxy_.get();
  }

  virtual void reset() override {
    resolved_receivers_.reset();
    local_interface_proxy_.reset();
    receiver_.reset();
  }

  virtual void force_resolve() override {
    (void)lookup();
  }

  virtual dependency_info create_dependency_info() const override {
    return {dependency_info::IMPORT, dependency_info::INTERFACE, get_message_info<InterfaceType>(), {receiver_.get()}};
  }

private:
  broker& broker_;
  component& owning_component_;
  std::weak_ptr<message_receivers> resolved_receivers_;
  std::unique_ptr<InterfaceType> local_interface_proxy_;
  std::shared_ptr<component> receiver_; // shared_ptr as a performance improvement I believe. TODO: measure
};

}

#endif // MINICOMPS_INTERFACE_REF_H_

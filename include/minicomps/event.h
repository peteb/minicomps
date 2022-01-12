/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_EVENT_H_
#define MINICOMPS_EVENT_H_

#include <minicomps/poly_ref.h>

namespace mc {

/// An event is like a query except it's sent to 0..* recipients and without a response.
template<typename MessageType>
class event {
public:
  event(poly_ref_base<MessageType>* handler, component* owning_component)
    : handler_(handler)
    , owning_component_(owning_component)
    , msg_info_(get_message_info<MessageType>()) {}

  void operator() (MessageType&& event) {
    component_listener* listener = owning_component_->listener;

    for (auto& receiver_handler : handler_->lookup()) {
      if (receiver_handler.mutual_executor()) {
        if (listener)
          listener->on_invoke(owning_component_, receiver_handler.receiver().get(), msg_info_, message_type::EVENT);

        receiver_handler.invoke(event);
      }
      else {
        if (listener)
          listener->on_enqueue(owning_component_, receiver_handler.receiver().get(), msg_info_, message_type::EVENT);

        // TODO: make sure we copy
        auto task = [handler = receiver_handler.handler()](void* data) {
          MessageType* event = static_cast<MessageType*>(data);
          (*handler)(*event);
        };

        receiver_handler.receiver()->executor->enqueue_work(std::move(task), event);
      }
    }
  }

private:
  poly_ref_base<MessageType>* handler_;
  component* owning_component_;
  const message_info& msg_info_;
};

}

#endif
// MINICOMPS_EVENT_H_

/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_ASYNC_EVENT_H_
#define MINICOMPS_ASYNC_EVENT_H_

#include <minicomps/poly_ref.h>

namespace mc {

/// An event is like a query except it's sent to 0..* recipients and without a response.
template<typename MessageType>
class async_event {
  poly_ref_base<MessageType>* handler_;

public:
  async_event(poly_ref_base<MessageType>* handler) : handler_(handler) {}

  void operator() (MessageType&& event) {
    for (auto& receiver_handler : handler_->lookup()) {
      if (receiver_handler.mutual_executor()) {
        // TODO: do we want this shortcut?
        receiver_handler.invoke(event);
      }
      else {
        // TODO: make sure we copy
        auto task = [handler = receiver_handler.handler()](void* data) {
          MessageType* event = static_cast<MessageType*>(data);
          (*handler)(*event);
        };

        receiver_handler.receiver()->executor->enqueue_work(std::move(task), event);
      }
    }
  }
};

}

#endif
// MINICOMPS_ASYNC_EVENT_H_

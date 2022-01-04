/// Copyright 2022 Peter Backman

#include <minicomps/broker.h>
#include <iostream>
namespace mc {

void broker::associate(message_id msgId, std::weak_ptr<component> comp) {
  std::lock_guard<std::recursive_mutex> lock(lookup_mutex_);
  auto iter = active_lookups_.find(msgId);
  if (iter == std::end(active_lookups_)) {
    message_receivers receivers;
    receivers.push_back(comp);
    active_lookups_[msgId] = std::make_shared<message_receivers>(std::move(receivers));
    return;
  }

  // There are already other handlers of this message, add us to the list after invalidating the old reference
  message_receivers receivers = *iter->second; // NOTE! This copy preserves immutability
  receivers.push_back(comp);
  iter->second = std::make_shared<message_receivers>(std::move(receivers));
}

void broker::disassociate(message_id msgId, component* comp) {
  std::lock_guard<std::recursive_mutex> lock(lookup_mutex_);

  auto iter = active_lookups_.find(msgId);
  if (iter == std::end(active_lookups_)) {
    std::abort();
    return;
  }

  message_receivers receivers = *iter->second; // NOTE! This copy preserves immutability

  for (auto iter = std::begin(receivers); iter != std::end(receivers); ) {
    const std::weak_ptr<component>& receiver = *iter;
    auto recv_sp = receiver.lock();

    if (!recv_sp || recv_sp.get() == comp)
      iter = receivers.erase(iter);
    else
      ++iter;
  }

  iter->second = std::make_shared<message_receivers>(std::move(receivers));
}

void broker::disassociate_everything(component* component) {
  std::lock_guard<std::recursive_mutex> lock(lookup_mutex_);

  for (auto& [msgId, _] : active_lookups_) {
    disassociate(msgId, component);
  }
}

std::weak_ptr<message_receivers> broker::lookup(message_id msgId) {
  std::lock_guard<std::recursive_mutex> lock(lookup_mutex_);

  auto iter = active_lookups_.find(msgId);
  if (iter == std::end(active_lookups_)) {
    std::shared_ptr<message_receivers> empty_receivers = std::make_shared<message_receivers>();
    active_lookups_[msgId] = empty_receivers;
    return empty_receivers;
  }

  return iter->second;
}

}

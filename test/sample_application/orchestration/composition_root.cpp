#include "composition_root.h"

#include <minicomps/broker.h>
#include <minicomps/executor.h>
#include <minicomps/component.h>

#include "user/user_system.h"
#include "session/session_system.h"

#include <sstream>

composition_root::composition_root()
  : executor_(std::make_shared<mc::executor>())
{
  components_.push_back(user_system::create_impl(broker_, executor_));
  components_.push_back(session_system::create_impl(broker_, executor_));

  // TODO: check that all dependencies have been fulfilled

  for (auto& component : components_) {
    component->publish();
  }
}

composition_root::~composition_root() {
  for (auto& component : components_) {
    component->unpublish();
  }
}

void composition_root::update() {

}

void composition_root::enable_sequence_diagram_gen() {
  for (auto& component : components_)
    component->listener = this;
}

std::string composition_root::dump_and_disable_sequence_diagram_gen() {
  for (auto& component : components_)
    component->listener = nullptr;

  return std::move(current_sequence_diagram_);
}

void composition_root::on_enqueue(const mc::component* sender, const mc::component* receiver, const mc::message_info& info, mc::message_type type) {
  std::stringstream ss;

  switch (type) {
  case mc::message_type::REQUEST:
    ss << sender->name << "->" << receiver->name << ": " << info.name << "\n";
    break;

  case mc::message_type::RESPONSE:
    ss << sender->name << "-->" << receiver->name << ": " << info.name << "\n";
    break;

  case mc::message_type::LOCKED_REQUEST:
    ss << sender->name << "->" << receiver->name << ": " << info.name << " (locked)\n";
    break;

  case mc::message_type::LOCKED_RESPONSE:
    ss << sender->name << "-->" << receiver->name << ": " << info.name << " (locked)\n";
    break;

  case mc::message_type::EVENT:
    ss << sender->name << "->" << receiver->name << ": " << info.name << " (event)\n";
    break;
  }

  current_sequence_diagram_ += ss.str();
}

void composition_root::on_invoke(const mc::component* sender, const mc::component* receiver, const mc::message_info& info, mc::message_type type) {
  std::stringstream ss;

  switch (type) {
  case mc::message_type::REQUEST:
    ss << sender->name << "->" << receiver->name << ": " << info.name << " (sync)\n";
    break;

  case mc::message_type::RESPONSE:
    ss << sender->name << "-->" << receiver->name << ": " << info.name << " (sync)\n";
    break;

  case mc::message_type::LOCKED_REQUEST:
    ss << sender->name << "->" << receiver->name << ": " << info.name << " (sync locked)\n";
    break;

  case mc::message_type::LOCKED_RESPONSE:
    ss << sender->name << "-->" << receiver->name << ": " << info.name << " (sync locked)\n";
    break;

  case mc::message_type::EVENT:
    ss << sender->name << "->" << receiver->name << ": " << info.name << " (sync event)\n";
    break;
  }

  current_sequence_diagram_ += ss.str();
}

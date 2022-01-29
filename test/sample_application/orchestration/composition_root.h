#pragma once

#include <minicomps/broker.h>
#include <minicomps/component.h>

#include <vector>
#include <memory>

namespace mc {
class component;
class executor;
}

class composition_root : public mc::component_listener {
public:
  composition_root();
  ~composition_root();

  template<typename T>
  std::shared_ptr<T> add_component() {
    auto component = std::make_shared<T>(broker_, executor_);
    components_.push_back(component);
    component->publish();
    return component;
  }

  void enable_sequence_diagram_gen();
  std::string dump_and_disable_sequence_diagram_gen();

  virtual void on_enqueue(const mc::component* sender, const mc::component* receiver, const mc::message_info& info, mc::message_type type) override;
  virtual void on_invoke(const mc::component* sender, const mc::component* receiver, const mc::message_info& info, mc::message_type type) override;

  void update();

private:
  mc::broker broker_;
  std::vector<std::shared_ptr<mc::component>> components_;
  std::shared_ptr<mc::executor> executor_;
  std::string current_sequence_diagram_;
};

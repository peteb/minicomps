#include "composition_root.h"

#include <minicomps/broker.h>
#include <minicomps/executor.h>
#include <minicomps/component.h>

#include "user/user_system.h"
#include "session/session_system.h"

#include <sstream>
#include <unordered_map>
#include <unordered_set>

composition_root::composition_root()
  : executor_(std::make_shared<mc::executor>())
{
  components_.push_back(user_system::create_impl(broker_, executor_));
  components_.push_back(session_system::create_impl(broker_, executor_));

  for (auto& component : components_) {
    component->allow_direct_call_async = false;
    component->publish();
  }
}

composition_root::~composition_root() {
  for (auto& component : components_) {
    component->unpublish();
  }
}

void composition_root::update() {
  executor_->execute();
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

std::vector<dependency> composition_root::get_missing_dependencies() {
  std::unordered_set<mc::message_id> exported_interfaces;

  for (std::shared_ptr<mc::component>& component : components_) {
    std::vector<mc::dependency_info> dependencies = component->describe_dependencies();

    for (mc::dependency_info& dependency : dependencies) {
      if (dependency.type == mc::dependency_info::INTERFACE && dependency.direction == mc::dependency_info::EXPORT)
        exported_interfaces.insert(dependency.msg_info.id);
    }
  }

  std::vector<dependency> missing_dependencies;

  for (std::shared_ptr<mc::component>& component : components_) {
    std::vector<mc::dependency_info> dependencies = component->describe_dependencies();

    for (mc::dependency_info& dependency : dependencies) {
      if (dependency.type == mc::dependency_info::INTERFACE && dependency.direction == mc::dependency_info::IMPORT) {
        if (exported_interfaces.count(dependency.msg_info.id) == 0)
          missing_dependencies.push_back({component, dependency.msg_info});
      }
    }
  }

  return missing_dependencies;
}

bool composition_root::verify_dependencies() {
  std::vector<dependency> missing_dependencies = get_missing_dependencies();

  for (dependency& dependency : missing_dependencies) {
    std::cout << "Component " << dependency.consumer->name << " has an unresolved interface dependency to '" << dependency.info.name << "'" << std::endl;
  }

  return missing_dependencies.empty();
}

std::string composition_root::dump_dependency_graph() {
  std::stringstream ss;

  ss << "digraph {\n";
  ss << "rankdir=LR;\n";

  // TODO: extract this into a separate dependency graph
  std::unordered_map<mc::message_id, std::shared_ptr<mc::component>> interface_implementors;

  // Collect interface implementors
  for (std::shared_ptr<mc::component>& component : components_) {
    std::vector<mc::dependency_info> dependencies = component->describe_dependencies();

    for (mc::dependency_info& dependency : dependencies) {
      if (dependency.type == mc::dependency_info::INTERFACE && dependency.direction == mc::dependency_info::EXPORT) {
        interface_implementors[dependency.msg_info.id] = component;
      }
    }
  }

  // Group by type
  std::unordered_map<const char*, std::vector<std::shared_ptr<mc::component>>> components_per_type;

  for (std::shared_ptr<mc::component>& component : components_) {
    std::vector<mc::dependency_info> dependencies = component->describe_dependencies();

    bool has_type = false;

    for (mc::dependency_info& dependency : dependencies) {
      if (dependency.type == mc::dependency_info::GROUP && dependency.direction == mc::dependency_info::EXPORT) {
        components_per_type[dependency.msg_info.name].push_back(component);
        has_type = true;
      }
    }
    // TODO: Clean this up
    if (!has_type)
      components_per_type[""].push_back(component);
  }

  int cluster_count = 0;

  // Go through imports and create arrows
  for (auto& [type_name, components] : components_per_type) {
    if (type_name[0]) {
      ss << "subgraph cluster_" << cluster_count++ << " {\n";
      ss << "label = \"" << type_name << "\";\n";
    }

    // Print the nodes first
    for (std::shared_ptr<mc::component>& component : components) {
      ss << component->name << "\n";
    }

    if (type_name[0])
      ss << "}\n";

    for (std::shared_ptr<mc::component>& component : components) {
      std::vector<mc::dependency_info> dependencies = component->describe_dependencies();

      for (mc::dependency_info& dependency : dependencies) {
        if (dependency.type == mc::dependency_info::INTERFACE && dependency.direction == mc::dependency_info::IMPORT) {
          if (std::shared_ptr<mc::component> implementor = interface_implementors[dependency.msg_info.id]) {
            ss << "\"" << component->name << "\" -> \"" << implementor->name << "\"\n";
          }
          else {
            ss << "\"" << component->name << "\" -> \"" << dependency.msg_info.name << "\" [label = \"missing impl\"]\n";
          }

        }
      }
    }
  }

  ss << "}\n";

  return ss.str();
}

#include "user_system.h"
#include "component_types.h"

#include <minicomps/component_base.h>

#include <unordered_map>
#include <optional>

namespace user_system {

class user_system_impl : public mc::component_base<user_system_impl, component_types::service> {
public:
  user_system_impl(mc::broker& broker, mc::executor_ptr executor)
    : component_base("user_system_impl", broker, executor)
    {}

  virtual void publish() override {
    publish_interface(if_);
    publish_async_query(if_.create_user, &user_system_impl::create_user);
    publish_async_query(if_.get_user, &user_system_impl::get_user);
  }

  mc::coroutine<int> create_user(const user_info& new_user) {
    return mc::make_successful_coroutine<int>(123);
  }

  mc::coroutine<std::optional<user_info>> get_user(std::string user_name) {
    std::cout << "User " << user_name << " was requested" << std::endl;
    user_info user;
    user.password = "pass";
    return mc::make_successful_coroutine<std::optional<user_info>>(std::optional<user_info>{user});
  }

private:
  // External interfaces
  interface if_;

  // State
  std::unordered_map<int, user_info> users_;
};

std::shared_ptr<mc::component> create_impl(mc::broker& broker, std::shared_ptr<mc::executor> executor) {
  return std::make_shared<user_system_impl>(broker, executor);
}

DEFINE_INTERFACE(interface);

}

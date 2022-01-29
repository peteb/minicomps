#pragma once

#include <minicomps/messaging.h>
#include <minicomps/if_async_query.h>

#include <string>
#include <optional>

namespace mc {
class component;
class broker;
class executor;
}

namespace user_system {

struct user_info {
  int id;
  std::string name;
  std::string password;
  int age;
};

DECLARE_EVENT(user_updated, {int id; });

///
/// Public interface
///
DECLARE_INTERFACE2(interface, {
  ASYNC_QUERY(create_user, int(user_info));
  ASYNC_QUERY(get_user, std::optional<user_info>(std::string name));
});

///
/// Constructor functions
///
std::shared_ptr<mc::component> create_impl(mc::broker& broker, std::shared_ptr<mc::executor> executor);

}

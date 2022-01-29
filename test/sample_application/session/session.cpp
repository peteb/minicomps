#include "session.h"
#include "user/user_system.h"

#include <iostream>

namespace session_system {

session::session(int id, const mc::interface<user_system::interface>& user_system)
  : id(id)
  , user_system_(user_system, lifetime_) // Copy the user_system reference but use our own lifetime, ensuring that we won't get a callback when we're gone
  {}

mc::coroutine<void> session::authenticate(const std::string& username, const std::string& password) {
  return user_system_->get_user(username)
    .then([this, password = std::move(password)] (std::optional<user_system::user_info>&& user) -> mc::result<void> {
      if (!user)
        return mc::failure(-2);

      if (user->password != password)
        return mc::failure(-1);

      std::cout << "created session" << std::endl;
      authenticated_ = true;
      return {};
    });
}

}

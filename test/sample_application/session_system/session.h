#pragma once

#include <minicoros/coroutine.h>
#include <minicomps/interface.h>
#include "user/user_system.h"
#include <string>
#include <memory>

namespace session_system {

class session {
public:
  // TODO: how can we forward declare the interface here
  // TODO: can we in some way make these interfaces mockable using gmock?
  session(int id, const mc::interface<user_system::interface>& user_system);

  mc::coroutine<void> authenticate(const std::string& username, const std::string& password);

  int id;

private:
  mc::lifetime lifetime_;
  mc::interface<user_system::interface> user_system_;
  bool authenticated_ = false;
};

}
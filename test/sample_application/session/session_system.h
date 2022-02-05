#pragma once

#include <minicomps/messaging.h>
#include <minicomps/if_async_query.h>
#include <minicomps/if_sync_query.h>

#include <string>
#include <memory>
#include <vector>

namespace mc {
class component;
class broker;
class executor;
}

namespace session_system {

struct session_info {
  int id;
  std::string username;
};

// TODO: macro prefix
DECLARE_EVENT(session_created, {
  int id;
});

///
/// Public interface
///
DECLARE_INTERFACE2(interface, {
  /// Creates a session
  ASYNC_QUERY(create_session, int());

  /// Destroys session
  ASYNC_QUERY(destroy_session, void(int));

  /// Checks if user has a session
  ASYNC_QUERY(has_session, bool(std::string username));

  /// Starts authenticating a session
  ASYNC_QUERY(authenticate_session, void(int id, std::string username, std::string password));

  SYNC_QUERY(get_sessions, std::vector<session_info>(const std::string& name_pattern));
});

///
/// Constructor functions
///
std::shared_ptr<mc::component> create_impl(mc::broker& broker, std::shared_ptr<mc::executor> executor);

}

// TODO: INTERFACE rather than INTERFACE2
// TODO: INTERFACE rather than DECLARE_INTERFACE

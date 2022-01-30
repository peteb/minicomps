#include "session_system.h"
#include "user/user_system.h"
#include "session.h"

#include <minicomps/component_base.h>
#include <minicomps/interface.h>

#include <unordered_map>
#include <vector>

namespace session_system {

///
/// Implementation of the Session system
///
class session_system_impl : public mc::component_base<session_system_impl> {
public:
  session_system_impl(mc::broker& broker, mc::executor_ptr executor)
    : component_base("session_system_impl", broker, executor)
    , user_system_(lookup_interface<user_system::interface>())
    {}

  virtual void publish() override {
    publish_interface(if_);
    publish_async_query(if_.create_session, &session_system_impl::create_session);
    publish_async_query(if_.destroy_session, &session_system_impl::destroy_session);
    publish_async_query(if_.has_session, &session_system_impl::has_session);
    publish_async_query(if_.authenticate_session, &session_system_impl::authenticate_session);
  }

  mc::coroutine<int> create_session() {
    const int new_session_id = next_session_id_++;
    mc::lifetime lif;
    session new_session(new_session_id, user_system_); // TODO: remove this lookup function and instead use the copy ctor. This is better since it makes it possible to limit lifetime further in Session
    active_sessions_.push_back(std::move(new_session));
    return mc::make_successful_coroutine<int>(new_session_id);
  }

  mc::coroutine<void> destroy_session(int session_id) {
    for (auto iter = std::begin(active_sessions_); iter != std::end(active_sessions_); ) {
      if (iter->id == session_id) {
        active_sessions_.erase(iter);
        break;
      }
    }
    return mc::make_successful_coroutine<void>();
  }

  mc::coroutine<bool> has_session(std::string username) {
    return mc::make_successful_coroutine<bool>(false);
  }

  mc::coroutine<void> authenticate_session(int id, std::string username, std::string password) {
    session* sess = find_session(id);
    if (!sess)
      return mc::make_failed_coroutine<void>(-1);

    return sess->authenticate(username, password);
  }

private:
  session* find_session(int id) {
    for (session& sess : active_sessions_) {
      if (sess.id == id)
        return &sess;
    }

    return nullptr;
  }

private:
  // External interfaces
  interface if_;

  // Dependencies
  mc::interface<user_system::interface> user_system_;

  // State
  std::vector<session> active_sessions_;
  int next_session_id_ = 1;
};

std::shared_ptr<mc::component> create_impl(mc::broker& broker, std::shared_ptr<mc::executor> executor) {
  return std::make_shared<session_system_impl>(broker, executor);
}

DEFINE_INTERFACE(interface);

}
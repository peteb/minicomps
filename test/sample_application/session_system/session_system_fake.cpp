#include "session_system.h"
#include "user/user_system.h"
#include "session.h"
#include "component_types.h"

#include <minicomps/component_base.h>
#include <minicomps/interface.h>
#include <minicomps/messaging.h>

#include <unordered_map>
#include <vector>

namespace session_system {

///
/// Fake Session system
///
class session_system_fake : public mc::component_base<session_system_fake, component_types::session> {
public:
  session_system_fake(mc::broker& broker, mc::executor_ptr executor)
    : component_base("session_system_fake", broker, executor)
    {}

  virtual void publish() override {
    // TODO: rename to just "publish"?
    publish_interface(if_);
    publish_async_query(if_.create_session, &session_system_fake::create_session);
    publish_async_query(if_.destroy_session, &session_system_fake::destroy_session);
    publish_async_query(if_.has_session, &session_system_fake::has_session);
    publish_async_query(if_.authenticate_session, &session_system_fake::authenticate_session);
    publish_sync_query(if_.get_sessions, &session_system_fake::get_sessions);
    publish_volatile_sync_query(if_.get_sessions_ref, &session_system_fake::get_sessions_ref);

    publish_interface(fake_if_);
    publish_async_query(fake_if_.setup_basic_sessions, &session_system_fake::setup_basic_sessions);
  }

  mc::coroutine<void> setup_basic_sessions() {
    return mc::make_successful_coroutine<void>();
  }
  
  mc::coroutine<int> create_session() {
    const int new_session_id = next_session_id_++;
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

    return mc::make_successful_coroutine<void>();
  }

  std::vector<session_info> get_sessions(const std::string& pattern) {
    std::vector<session_info> result;
    for (const session& sess : active_sessions_)
      result.push_back({sess.id});

    return result;
  }

  std::vector<session_info>& get_sessions_ref(const std::string& pattern) {
    static std::vector<session_info> result;
    result.clear();
    for (const session& sess : active_sessions_)
      result.push_back({sess.id});

    return result;
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
  fake fake_if_;

  // Events
  mc::event<session_created> event_session_created_ = lookup_event<session_created>();

  // State
  std::vector<session> active_sessions_;
  int next_session_id_ = 1;
};

std::shared_ptr<mc::component> create_fake(mc::broker& broker, std::shared_ptr<mc::executor> executor) {
  return std::make_shared<session_system_fake>(broker, executor);
}

}

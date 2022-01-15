/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_COROUTINE_QUERY_H_
#define MINICOMPS_COROUTINE_QUERY_H_

#include <minicomps/async_query.h>

#include <minicoros/coroutine.h>

namespace mc {

template<typename MessageType>
class coroutine_query {
public:
  coroutine_query() = delete;
  coroutine_query(const coroutine_query&) = delete;
  coroutine_query(async_query<MessageType>&& query) : query_(std::move(query)) {}
  coroutine_query(const async_query<MessageType>& query, const lifetime& life) : query_(query, life) {}
  coroutine_query(const coroutine_query<MessageType>& query, const lifetime& life) : query_(query.query_, life) {}

  template<typename... Args>
  mc::coroutine<int> operator() (Args&&... arguments) {
    return mc::coroutine<int>([this, arguments...](mc::promise<int>&& promise) mutable {
      query_(std::forward<Args>(arguments)...)
        .with_callback([promise = std::move(promise)] (mc::concrete_result<int>&& result) {
          promise(std::move(result));
        });
    });
  }

private:
  async_query<MessageType> query_;
};

}

#endif // MINICOMPS_COROUTINE_QUERY_H_

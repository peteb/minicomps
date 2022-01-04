# minicomps

**`Minicomps` is a C++17 library that implements a framework for sync and async communication between "components"**. It supports the following features:

- **Name-based function lookup** for loose coupling
- **Zero memory allocation** for queries
- **Automatic locking** on component level for synchronous queries
- All calls are **always thread safe** (unless explicitly overridden by the user)
- Periodical pumping and async operations can be skipped if the handling component has been synchronously locked (TODO)
- Aspect-oriented support for async invocations (can be used to inject errors, collect request duration, logging, testing, etc) (TODO)
- Startup dependency verification (TODO)
- Async function invocation using "minicoros" (a library that simplifies futures/promises) (TODO)
- Dependency reflection (can be used to enforce interaction policies, generate dependency graphs, etc) (TODO)

## Examples
```cpp
DECLARE_QUERY(Sum, int(int t1, int t2));
DECLARE_QUERY(UpdateValues, int(int new_value));

class receiver : public component_base<receiver> {
public:
  receiver(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_async_query<Sum>([](int t1, int t2, mc::callback_result<int>&& result) {
      result(t1 + t2);
    });

    publish_sync_query<UpdateValues>([this] (int new_value) {
      value1 = new_value;
      value2 = new_value;
      return value1 - value2;
    });
  }

private:
  int value1 = 0;
  int value2 = 0;
};

class sender : public component_base<sender> {
public:
  sender(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , sum_(lookup_async_query<Sum>())
    , update_values_(lookup_sync_query<UpdateValues>())
    {}

  void frob() {
    // Call the async query first
    sum_(1, 3).with_callback([this](mc::concrete_result<int> cr) {
      if (cr.successful()) {
        int sum = *cr.get_value();

        // Then update synchronously
        update_values_(sum);
      }
    });
  }

private:
  async_query<Sum> sum_;
  sync_query<UpdateValues> update_values_;
};
```

## Limitations and trade-offs
- Setting up and tearing down components isn't important from a performance perspective. Ie; it's OK to allocate many objects and take big locks.
- Sync queries should be decently fast, at least 100k calls per millisecond.
- No queries should allocate dynamic memory.
- Sync queries should not copy the parameters.
- Async queries can only lock the executor.

## Performance
As measured using Docker on my MacBook Pro from 2013. Might not be up-to-date, but shows ballpark figures.

| Operation                                                 | Rate       | Allocs/query |
|-----------------------------------------------------------|-----------:|--------------|
| Sync queries on the same executor                         | 143,000K/s | 0            |
| Sync queries across threads                               |  28,000K/s | 0            |
| Sync queries from 3 threads                               |   7,000K/s | 0            |
| Async queries on the same executor                        |  53,000K/s | 0            |
| Async queries across executor, same thread                |   3,600K/s | 0            |
| SPSC async queries from 1 thread to 1 receiver thread     |   1,600K/s | ~0           |
| MPSC async queries from 3 threads to 1 receiver thread    |   1,400K/s | ~0           |

Note 1: the asynchronous messaging system is almost as simple as they come; for example, the queue is based on std::vector protected by a std::recursive_mutex. It can probably be optimized.
Note 2: ~0 allocations for some async queries since executors allocate new memory when the queue is full (amortizes to 0)

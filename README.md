# minicomps

**`Minicomps` is a C++17 framework for synchronous and asynchronous communication between components**. It supports the following features:

- **Queries** for calling into other components to get a result or return value
- **Events** for raising events that have happened and for disseminating data
- Components are **decentralized and loosely coupled** due to name-based function lookup
- Queries and events do **no memory allocations**
- **Automatic locking** on component level for synchronous queries
- All actions are **thread safe** (unless explicitly overridden by the user)
- Periodical pumping and async operations can be skipped if the handling component has been synchronously locked (TODO)
- Aspect-oriented support for async invocations (can be used to inject errors, collect request duration, logging, testing, etc) (TODO)
- Startup dependency verification (TODO)
- Async function invocation using "minicoros" (a library that simplifies futures/promises) (TODO)
- Dependency reflection (can be used to enforce interaction policies, generate dependency graphs, etc) (TODO)

## Examples
```cpp
// Queries, like ordinary functions, have two parts: a declaration and a definition.
// This separation makes it possible to call queries across shared libraries.
DECLARE_QUERY(Sum, int(int t1, int t2)); DEFINE_QUERY(Sum);
DECLARE_QUERY(UpdateValues, int(int new_value)); DEFINE_QUERY(UpdateValues);

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

### Namespaced queries
```cpp
namespace math {
DECLARE_QUERY(Sum, int(int t1, int t2)); DEFINE_QUERY(Sum);
}

namespace output {
DECLARE_QUERY(PrintValue, void(int)); DEFINE_QUERY(Sum);
}

class calculator : public component_base<calculator> {
public:
  calculator(broker& broker, executor_ptr executor)
    : component_base("calculator", broker, executor)
    , print_value_(lookup_async_query(output::PrintValue))
    {}

  virtual void publish() override {
    publish_async_query<math::Sum>([this](int t1, int t2, mc::callback_result<int>&& result) {
      print_value_(t1 + t2);
      result(t1 + t2);
    });
  }

private:
  async_query<output::PrintValue> print_value_;
};
```

## Events
```cpp
DECLARE_EVENT(ContactUpdated, {
  int contactId;
});
DEFINE_EVENT(ContactUpdated);

class address_book : public component_base<address_book> {
public:
  address_book(broker& broker, executor_ptr executor)
    : component_base("address_book", broker, executor)
    , contact_updated_(lookup_async_event(ContactUpdated))
    {}

  void update_contact(int contactId) {
    // ... update the contact ...
    contact_updated_({contactId});
  }

private:
  async_event<ContactUpdated> contact_updated_;
};

class interested_party : public component_base<interested_party> {
public:
  interested_party(broker& broker, executor_ptr executor)
    : component_base("interested_party", broker, executor)
    {}

  virtual void publish() override {
    publish_async_event_listener<ContactUpdated>([this](const ContactUpdated& event) {
      std::cout << "Contact " << event.contactId << " updated" << std::endl;
    });
  }
};

```

## Limitations and trade-offs
- Setting up and tearing down components isn't important from a performance perspective. Ie; it's OK to allocate many objects and take big locks.
- Sync queries should be decently fast, at least 100k calls per millisecond.
- No queries should allocate dynamic memory.
- Sync queries should not copy the parameters.
- Async queries can only lock the executor.

## Performance
As measured on my MacBook Pro from 2013 through Docker. Might not be up-to-date, but shows ballpark figures.

| Operation                                                 | Rate       | Allocs/q     | Lock fails/q        |
|-----------------------------------------------------------|-----------:|--------------|--------------------:|
| Sync queries on the same executor                         | 143,000K/s | 0            | 0                   |
| Sync queries across threads                               |  28,000K/s | 0            | 0                   |
| Sync queries from 3 threads                               |   7,000K/s | 0            |                     |
| Async queries on the same executor                        |  53,000K/s | 0            | 0                   |
| Async queries across executor, same thread                |   3,600K/s | 0            | 0                   |
| Async queries SPSC from 1 thread to 1 receiver thread     |   2,800K/s | 0 (amortized)| ~0.5                |
| Async queries MPSC from 3 threads to 1 receiver thread    |   1,400K/s | 0 (amortized)| ~0.95               |
| Async events same executor                                | 120,000K/s | 0            | 0                   |
| Async events SPSC from 1 thread to 1 receiver thread      |   4,890K/s | 0 (amortized)| ~0.43               |

- Note: the asynchronous messaging system is almost as simple as they come; for example, the queue is based on std::vector protected by a std::mutex, and there's a fair bit of lock contention (as can be seen by the lock failures in the table above)


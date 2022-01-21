# minicomps

**`Minicomps` is a C++17 framework for synchronous and asynchronous communication between components**. It supports the following features:

- **Queries** for request/response patterns
- **Events** for pub/sub patterns
- Components are **decentralized and loosely coupled** due to name-based lookup
- Queries and events are **free from memory allocations**
- **Automatic locking** on component level for synchronous queries
- All actions are **thread safe** unless explicitly overridden by the user
- Asynchronous queries can be **canceled**, both manually and automatically, and the handling component can check for cancellations
- **Interfaces** for an OOP feeling
- **Filter handlers** can be registered for async queries (can be used for mocks, spies, error injection, etc)
- Per-query executor to enable **flow control**
- Async function invocation using **minicoros** (a library that simplifies futures/promises)
- Component-level listeners for invocations (can be used to collect request duration, logging, tracing, generating sequence diagrams, etc)
- Dependency reflection (can be used to enforce interaction policies, verify dependencies, generate dependency graphs, etc)
- Periodical pumping and async operations can be skipped if the handling component has been synchronously locked (TODO)

## Examples
```cpp
// Queries, like ordinary functions, have two parts: a declaration and a definition.
// This separation makes it possible to call queries across shared libraries.
DECLARE_QUERY(Sum, int(int t1, int t2)); DEFINE_QUERY(Sum);
DECLARE_QUERY(UpdateValues, int(int new_value)); DEFINE_QUERY(UpdateValues);
DECLARE_EVENT(UserUpdated, {int user_id; }); DEFINE_EVENT(UserUpdated);

class receiver : public component_base<receiver> {
public:
  receiver(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    // Async queries are functions that get passed a callback and can return later
    publish_async_query<Sum>(&receiver::sum);

    // Sync queries are direct invocation and lock the target component
    publish_sync_query<UpdateValues>(&receiver::update_values);

    // Events are structs that are sent out 1:n
    subscribe_event<UserUpdated>(&receiver::on_user_updated);
  }

  int sum(int t1, int t2, mc::callback_result<int>&& result) {
    // Callbacks can fail or return successfully
    result(t1 + t2);
  }

  int update_values(int new_value) {
    value1 = new_value;
    value2 = new_value;
    return value1 - value2;
  }

  void on_user_updated(const UserUpdated& info) {
    // ...
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

### Interfaces
```cpp
DECLARE_INTERFACE(receiver); DEFINE_INTERFACE(receiver);

class receiver {
public:
  ASYNC_QUERY(frobnicate, void(int));
};

class receiver_component_impl : public component_base<receiver_component_impl> {
public:
  receiver_component_impl(broker& broker, executor_ptr executor)
    : component_base("receiver_component", broker, executor)
    {}

  virtual void publish() override {
    publish_interface(receiver_);
    publish_async_query(receiver_.frobnicate, &receiver_component_impl::frobnicate_impl);
  }

  void frobnicate_impl(int value, callback_result<void>&& result) {
    // ...
  }

private:
  receiver receiver_;
};


class sender_component_impl : public component_base<sender_component_impl> {
public:
  sender_component_impl(broker& broker, executor_ptr executor)
    : component_base("sender_component", broker, executor)
    , receiver_(lookup_interface<receiver>())
  {}

  void frob() {
    receiver_->frobnicate(123)
      .then([] {
        // ...
      });
  }

private:
  interface<receiver> receiver_;
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

### Events
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
    subscribe_event<ContactUpdated>([this](const ContactUpdated& event) {
      std::cout << "Contact " << event.contactId << " updated" << std::endl;
    });
  }
};
```

### Cancellation
```c++
DECLARE_QUERY(LongOperation, int()); DEFINE_QUERY(LongOperation);

class receiver : public component_base<receiver> {
public:
  receiver(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    publish_async_query<LongOperation>(&receiver::long_operation);
  }

  void long_operation(mc::callback_result<int>&& result) {
    if (result.canceled()) {
      // The sender went out of scope or explicitly canceled the request
    }

    // We can still return a result. It'll be ignored though.
    result(123);
  }
};

class sender : public component_base<sender> {
public:
  sender(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , long_operation_(lookup_async_query<LongOperation>())
    {}

  void frob() {
    long_operation_()
      .with_lifetime(operation_lifetime_)
      .with_callback([] {
        // This callback won't get invoked if `operation_lifetime` has expired
      });

    operation_lifetime_.reset(); // Expire/cancel
  }

private:
  async_query<LongOperation> long_operation_;
  lifetime operation_lifetime_;
};
```

### Coroutines and Sessions
```c++
DECLARE_QUERY(LongOperation, int(int));

class send_component : public component_base<send_component> {
public:
  send_component(broker& broker, executor_ptr executor)
    : component_base("sender", broker, executor)
    , long_operation(lookup_async_query<LongOperation>())
    {}

  class session {
  public:
    session(const async_query<LongOperation>& long_operation)  // Inject the function that the session needs
      : long_operation_(long_operation, lifetime_)             // We cannot capture the function without providing our own lifetime
      {}

    void frob() {
      long_operation_(123)
        .then([this](int value) {
          // Won't get called if our session object has been deleted
        });
    }

  private:
    lifetime lifetime_;
    coroutine_query<LongOperation> long_operation_;
  };

  async_query<LongOperation> long_operation;
  std::shared_ptr<session> current_session = std::make_shared<session>(long_operation);
};
```

### Flow control
```c++
DECLARE_QUERY(LongOperation, int()); DEFINE_QUERY(LongOperation);

class receiver : public component_base<receiver> {
public:
  receiver(broker& broker, executor_ptr executor)
    : component_base("receiver", broker, executor)
    {}

  virtual void publish() override {
    // By overriding the executor used for this query, the component can control its execution
    publish_async_query<LongOperation>(&receiver::long_operation, operation_executor_);
  }

  void long_operation(mc::callback_result<int>&& result) {
    result(123);
  }

  void update() {
    if (time_to_run_operations())
      operation_executor_->execute();
  }

private:
  executor_ptr operation_executor_ = std::make_shared<executor>();
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
| Async queries on the same executor                        |  25,000K/s | 0            | 0                   |
| Async queries across executor, same thread                |   3,300K/s | 0            | 0                   |
| Async queries SPSC from 1 thread to 1 receiver thread     |   2,800K/s | 0 (amortized)| ~0.5                |
| Async queries MPSC from 3 threads to 1 receiver thread    |   1,400K/s | 0 (amortized)| ~0.95               |
| Sync events same executor                                 | 120,000K/s | 0            | 0                   |
| Async events SPSC from 1 thread to 1 receiver thread      |   4,890K/s | 0 (amortized)| ~0.43               |

- Note: the asynchronous messaging system is almost as simple as they come; for example, the queue is based on std::vector protected by a std::mutex, and there's a fair bit of lock contention (as can be seen by the lock failures in the table above)

/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_LIFETIME_H
#define MINICOMPS_LIFETIME_H

#include <memory>

namespace mc {

using lifetime_weak_ptr = std::weak_ptr<int>;

class lifetime {
public:
  lifetime_weak_ptr create_weak_ptr() const {
    return std::weak_ptr<int>(ptr_);
  }

  void reset() {
    ptr_ = std::make_shared<int>();
  }

private:
  std::shared_ptr<int> ptr_ = std::make_shared<int>();
};

}

#endif // MINICOMPS_LIFETIME_H

/// Copyright 2022 Peter Backman

#ifndef MINICOMPS_INTERFACE_H
#define MINICOMPS_INTERFACE_H

#include <minicomps/interface_ref.h>

namespace mc {

template<typename InterfaceType>
class interface {
public:
  interface(interface_ref_base<InterfaceType>* ref)
    : ref_(ref)
    {}

  interface(const interface& other, lifetime life)
    : ref_(other.ref_->clone(life))
  {
  }

  InterfaceType* operator-> () {
    // TODO: support for fallback?
    // TODO: lookup can return null, handle that
    return ref_->lookup();
  }

private:
  interface_ref_base<InterfaceType>* ref_;
};

}

#endif // MINICOMPS_INTERFACE_H

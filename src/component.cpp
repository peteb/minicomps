/// Copyright 2022 Peter Backman

#include <minicomps/lifetime.h>

#include <memory>

namespace mc {

class component;
class lifetime;

thread_local component* current_component = nullptr;
thread_local lifetime_weak_ptr current_lifetime;

void set_current_component(component* comp) {
  current_component = comp;
}

component* get_current_component() {
  return current_component;
}

void set_current_lifetime(lifetime_weak_ptr life) {
  current_lifetime = life;
}

lifetime_weak_ptr get_current_lifetime() {
  return current_lifetime;
}

}
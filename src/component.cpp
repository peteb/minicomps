/// Copyright 2022 Peter Backman

#include <minicomps/component.h>

namespace mc {

thread_local component* current_component = nullptr;

void set_current_component(component* comp) {
  current_component = comp;
}

component* get_current_component() {
  return current_component;
}

}
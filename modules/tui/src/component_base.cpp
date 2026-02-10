#include "tui/component_base.hpp"
#include <algorithm>

namespace nazg::tui {

void ComponentBase::add_child(std::unique_ptr<ComponentBase> child) {
  if (!child) {
    return;
  }

  child->set_parent(this);
  child->on_mount();
  children_.push_back(std::move(child));
}

bool ComponentBase::remove_child(const std::string& child_id) {
  auto it = std::find_if(children_.begin(), children_.end(),
                         [&child_id](const auto& child) {
                           return child->id() == child_id;
                         });

  if (it == children_.end()) {
    return false;
  }

  (*it)->on_unmount();
  (*it)->set_parent(nullptr);
  children_.erase(it);
  return true;
}

} // namespace nazg::tui

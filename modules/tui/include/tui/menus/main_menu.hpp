#pragma once

#include "tui/menu.hpp"

namespace nazg {
namespace blackbox {
class logger;
}

namespace nexus {
class Store;
}

namespace tui {

class MainMenu : public Menu {
public:
  MainMenu(nazg::blackbox::logger* log = nullptr);

  std::string id() const override { return "main"; }
  std::string title() const override { return "Main Menu"; }

  void build(TUIContext& ctx) override;

private:
  nazg::blackbox::logger* log_;
  TUIContext* ctx_ = nullptr;
  int selected_index_ = 0;

  std::vector<std::string> menu_items_ = {
    "Docker Management",
    "System Tools",
    "Git Management",
    "Settings",
    "Exit"
  };

  void on_item_selected(int index);
};

} // namespace tui
} // namespace nazg

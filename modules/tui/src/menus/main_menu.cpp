#include "tui/menus/main_menu.hpp"
#include "tui/ftxui_component.hpp"
#include "tui/tui_context.hpp"
#include "blackbox/logger.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

namespace nazg::tui {

MainMenu::MainMenu(nazg::blackbox::logger* log)
    : log_(log) {}

void MainMenu::build(TUIContext& ctx) {
  ctx_ = &ctx;

  // Create render function
  auto render_fn = [this]() -> Element {
    std::vector<Element> elements;

    // Title
    elements.push_back(text("═══ NAZG Control Center ═══") | bold | center | bgcolor(Color::Blue));
    elements.push_back(separator());
    elements.push_back(text(""));

    // Menu items
    for (size_t i = 0; i < menu_items_.size(); ++i) {
      bool selected = (i == static_cast<size_t>(selected_index_));

      std::string line = selected ? "▶ " : "  ";
      line += menu_items_[i];

      auto elem = text(line);
      if (selected) {
        elem = elem | bold | bgcolor(Color::Cyan) | color(Color::Black);
      }
      elements.push_back(elem);
    }

    elements.push_back(text(""));
    elements.push_back(separator());
    elements.push_back(text("↑↓: Navigate | Enter: Select | :back/:home: Exit") | dim | center);

    return vbox(elements) | center;
  };

  // Create event handler
  auto event_fn = [this](const Event& event) -> bool {
    // Arrow navigation
    if (event == Event::ArrowUp) {
      if (selected_index_ > 0) {
        selected_index_--;
      }
      return true;
    }

    if (event == Event::ArrowDown) {
      if (selected_index_ < static_cast<int>(menu_items_.size()) - 1) {
        selected_index_++;
      }
      return true;
    }

    // Enter to select
    if (event == Event::Return) {
      on_item_selected(selected_index_);
      return true;
    }

    return false;
  };

  // Create interactive FTXUI component wrapper and set as root
  auto component = std::make_unique<FTXUIComponent>("main-menu", render_fn, event_fn);
  set_root(std::move(component));

  if (log_) {
    log_->info("MainMenu", "Main menu built successfully");
  }
}

void MainMenu::on_item_selected(int index) {
  if (!ctx_) return;

  switch (index) {
    case 0:
      // Docker Management
      if (log_) {
        log_->info("MainMenu", "Loading Docker menu");
      }
      ctx_->commands().execute(*ctx_, "load", {"docker"});
      break;
    case 1:
      // System Tools
      if (log_) {
        log_->info("MainMenu", "System Tools not yet implemented");
      }
      ctx_->set_status_message("System Tools - Coming Soon");
      break;
    case 2:
      // Git Management
      if (log_) {
        log_->info("MainMenu", "Git Management not yet implemented");
      }
      ctx_->set_status_message("Git Management - Coming Soon");
      break;
    case 3:
      // Settings
      if (log_) {
        log_->info("MainMenu", "Settings not yet implemented");
      }
      ctx_->set_status_message("Settings - Coming Soon");
      break;
    case 4:
      // Exit
      if (log_) {
        log_->info("MainMenu", "Exiting TUI");
      }
      ctx_->commands().execute(*ctx_, "quit", {});
      break;
  }
}

} // namespace nazg::tui

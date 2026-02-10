#pragma once

#include <ftxui/screen/color.hpp>
#include <string>

namespace nazg::tui {

/**
 * @brief TUI color theme
 */
struct Theme {
  std::string name;

  // Pane colors
  ftxui::Color active_border;
  ftxui::Color inactive_border;
  ftxui::Color pane_bg;
  ftxui::Color pane_fg;

  // Status bar colors
  ftxui::Color status_bg;
  ftxui::Color status_fg;
  ftxui::Color status_accent;

  // UI elements
  ftxui::Color highlight;
  ftxui::Color dimmed;
};

/**
 * @brief Cyberpunk purple theme
 */
Theme cyberpunk_theme();

/**
 * @brief Default theme (gruvbox-inspired)
 */
Theme default_theme();

/**
 * @brief Get theme by name
 */
Theme get_theme(const std::string& name);

} // namespace nazg::tui

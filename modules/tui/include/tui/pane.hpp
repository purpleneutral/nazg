#pragma once

#include "tui/terminal.hpp"
#include <ftxui/component/component.hpp>
#include <memory>
#include <string>
#include <vector>

namespace nazg::tui {

/**
 * @brief Pane represents a single terminal pane in the TUI
 *
 * A pane wraps a Terminal and handles:
 * - Terminal output buffering and rendering
 * - Scrollback history
 * - Active/inactive state
 */
class Pane {
public:
  /**
   * @brief Create a new pane running the specified command
   * @param cmd Command to run (e.g., "/bin/zsh")
   * @param args Arguments for the command
   */
  Pane(const std::string& cmd, const std::vector<std::string>& args = {});

  /**
   * @brief Check if the pane's process is still alive
   */
  bool is_alive() const;

  /**
   * @brief Get the PID of the pane's process
   */
  pid_t pid() const;

  /**
   * @brief Send input to the pane
   */
  void send_input(const std::string& data);

  /**
   * @brief Read any new output from the terminal
   * Should be called regularly to update the pane's buffer
   */
  void update();

  /**
   * @brief Resize the pane
   */
  void resize(int width, int height);

  /**
   * @brief Get the rendered content as FTXUI component
   * @param active Whether this pane is currently active
   * @param active_color Border color for active pane
   * @param inactive_color Border color for inactive pane
   */
  ftxui::Element render(bool active, ftxui::Color active_color, ftxui::Color inactive_color) const;

  /**
   * @brief Get the terminal output buffer
   */
  const std::string& buffer() const { return buffer_; }

  /**
   * @brief Clear the buffer
   */
  void clear_buffer();

  /**
   * @brief Get scrollback lines
   */
  const std::vector<std::string>& scrollback() const { return scrollback_; }

  /**
   * @brief Set active state
   */
  void set_active(bool active) { active_ = active; }

  /**
   * @brief Get active state
   */
  bool is_active() const { return active_; }

private:
  std::unique_ptr<Terminal> terminal_;
  std::string buffer_;
  std::vector<std::string> scrollback_;
  bool active_ = false;

  int width_ = 80;
  int height_ = 24;

  static constexpr size_t MAX_SCROLLBACK_LINES = 10000;

  /**
   * @brief Process output and add to buffer/scrollback
   */
  void process_output(const std::string& data);

  /**
   * @brief Parse buffer into lines for scrollback
   */
  void update_scrollback();
};

} // namespace nazg::tui

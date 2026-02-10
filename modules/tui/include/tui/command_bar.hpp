#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <string>
#include <vector>

namespace nazg::tui {

class TUIContext;

/**
 * @brief Minimal command bar inspired by ctrlCore's command palette.
 *
 * When active, it captures keyboard input, allows editing the buffer,
 * and executes commands via the TUIContext CommandManager on Enter.
 */
class CommandBar {
public:
  CommandBar() = default;

  /**
   * @brief Activate command bar and clear buffer.
   */
  void activate();

  /**
   * @brief Deactivate command bar without executing buffer.
   */
  void deactivate();

  /**
   * @brief Whether the command bar is currently capturing input.
   */
  bool active() const { return active_; }

  /**
   * @brief Handle keyboard event. Returns true if consumed.
   */
  bool handle_event(const ftxui::Event& event, TUIContext& ctx);

  /**
   * @brief Render the command bar element.
   */
  ftxui::Element render(int width) const;

private:
  bool execute_buffer(TUIContext& ctx);

  bool active_ = false;
  std::string buffer_;
};

} // namespace nazg::tui

#include "tui/command_bar.hpp"

#include "tui/tui_context.hpp"
#include "tui/managers/command_manager.hpp"
#include <cctype>
#include <sstream>
#include <vector>

namespace nazg::tui {

void CommandBar::activate() {
  active_ = true;
  buffer_.clear();
}

void CommandBar::deactivate() {
  active_ = false;
  buffer_.clear();
}

bool CommandBar::handle_event(const ftxui::Event& event, TUIContext& ctx) {
  if (!active_) {
    return false;
  }

  if (event == ftxui::Event::Escape) {
    ctx.log_info("CommandBar cancelled");
    deactivate();
    ctx.set_status_message("Command cancelled");
    return true;
  }

  if (event == ftxui::Event::Backspace) {
    if (!buffer_.empty()) {
      buffer_.pop_back();
    }
    return true;
  }

  if (event == ftxui::Event::Return) {
    return execute_buffer(ctx);
  }

  if (event.is_character()) {
    const std::string& ch = event.character();
    if (!ch.empty() && std::isprint(static_cast<unsigned char>(ch[0]))) {
      buffer_ += ch;
      return true;
    }
  }

  return false;
}

bool CommandBar::execute_buffer(TUIContext& ctx) {
  std::istringstream iss(buffer_);
  std::string command;
  iss >> command;

  if (command.empty()) {
    ctx.log_warn("CommandBar executed empty command");
    ctx.set_status_message("No command entered");
    deactivate();
    return true;
  }

  std::vector<std::string> args;
  std::string arg;
  while (iss >> arg) {
    args.push_back(arg);
  }

  ctx.log_info("CommandBar executing: " + command);
  bool success = ctx.commands().execute(ctx, command, args);
  if (success) {
    ctx.set_status_message("Command: " + command);
    ctx.log_info("Command succeeded: " + command);
  } else {
    ctx.set_status_message("Command failed: " + command);
    ctx.log_warn("Command failed: " + command);
  }

  deactivate();
  return true;
}

ftxui::Element CommandBar::render(int width) const {
  using namespace ftxui;

  if (!active_) {
    // Render a subtle inactive bar to keep layout stable.
    return hbox(text(":"), filler()) | size(WIDTH, GREATER_THAN, width);
  }

  std::string display = ":" + buffer_;
  return hbox({
             text(display) | bold,
             filler(),
           }) |
         inverted |
         size(WIDTH, GREATER_THAN, width);
}

} // namespace nazg::tui

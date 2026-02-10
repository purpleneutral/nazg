#include "tui/pane.hpp"
#include <ftxui/dom/elements.hpp>
#include <algorithm>
#include <sstream>

namespace nazg::tui {

using namespace ftxui;

Pane::Pane(const std::string& cmd, const std::vector<std::string>& args)
    : terminal_(std::make_unique<Terminal>(cmd, args, width_, height_)) {
}

bool Pane::is_alive() const {
  return terminal_ && terminal_->is_alive();
}

pid_t Pane::pid() const {
  return terminal_ ? terminal_->pid() : -1;
}

void Pane::send_input(const std::string& data) {
  if (terminal_) {
    terminal_->write(data);
  }
}

void Pane::update() {
  if (!terminal_) {
    return;
  }

  char buffer[4096];
  ssize_t n;

  while ((n = terminal_->read(buffer, sizeof(buffer))) > 0) {
    std::string data(buffer, n);
    process_output(data);
  }
}

void Pane::resize(int width, int height) {
  width_ = width;
  height_ = height;

  if (terminal_) {
    terminal_->resize(width, height);
  }
}

Element Pane::render(bool active, Color active_color, Color inactive_color) const {
  // Split buffer into lines
  std::vector<std::string> lines;
  std::istringstream iss(buffer_);
  std::string line;

  while (std::getline(iss, line)) {
    lines.push_back(line);
  }

  // Take last N lines that fit in the pane
  int visible_lines = std::min(height_, static_cast<int>(lines.size()));
  int start_line = std::max(0, static_cast<int>(lines.size()) - visible_lines);

  std::vector<Element> content;
  for (int i = start_line; i < static_cast<int>(lines.size()); ++i) {
    content.push_back(text(lines[i]));
  }

  // Pad with empty lines if needed
  while (static_cast<int>(content.size()) < height_) {
    content.push_back(text(""));
  }

  Element inner = vbox(std::move(content));

  // Add border with theme colors
  Color border_color = active ? active_color : inactive_color;
  return window(text(""), inner) | border | color(border_color);
}

void Pane::clear_buffer() {
  buffer_.clear();
}

void Pane::process_output(const std::string& data) {
  buffer_ += data;

  // Keep buffer size manageable
  const size_t MAX_BUFFER_SIZE = 100000;
  if (buffer_.size() > MAX_BUFFER_SIZE) {
    // Keep last portion
    buffer_ = buffer_.substr(buffer_.size() - MAX_BUFFER_SIZE / 2);
  }

  update_scrollback();
}

void Pane::update_scrollback() {
  // Parse buffer into lines and add to scrollback
  std::istringstream iss(buffer_);
  std::string line;
  std::vector<std::string> new_lines;

  while (std::getline(iss, line)) {
    new_lines.push_back(line);
  }

  // Update scrollback with new lines
  if (!new_lines.empty()) {
    scrollback_.insert(scrollback_.end(), new_lines.begin(), new_lines.end());

    // Trim scrollback if too large
    if (scrollback_.size() > MAX_SCROLLBACK_LINES) {
      scrollback_.erase(
          scrollback_.begin(),
          scrollback_.begin() + (scrollback_.size() - MAX_SCROLLBACK_LINES));
    }
  }
}

} // namespace nazg::tui

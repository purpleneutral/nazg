#pragma once
#include "prompt/colors.hpp"
#include "prompt/icons.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::directive {
struct context;
}

namespace nazg::prompt {

enum class Style {
  MINIMAL,   // Compact, single-line where possible
  STANDARD,  // Box with essential context (default)
  VERBOSE    // Full context panel with timestamps
};

struct PromptContext {
  std::string title;
  std::string project_name;
  std::string project_path;
  std::vector<std::pair<std::string, std::string>> facts;
  std::string status_message;
  bool show_timestamp = false;
};

struct PromptContent {
  std::string question;
  std::vector<std::string> actions;
  std::vector<std::string> warnings;
  std::vector<std::string> infos;
  std::vector<std::string> details;
};

class Prompt {
public:
  explicit Prompt(nazg::blackbox::logger* log = nullptr);

  // ── Context ──
  Prompt& title(const std::string& command_name);
  Prompt& project(const std::string& name, const std::string& path);
  Prompt& fact(const std::string& key, const std::string& value);
  Prompt& status(const std::string& message);
  Prompt& timestamp(bool show = true);

  // ── Content ──
  Prompt& question(const std::string& text);
  Prompt& action(const std::string& description);
  Prompt& warning(const std::string& text);
  Prompt& info(const std::string& text);
  Prompt& detail(const std::string& text);

  // ── Display & Input ──
  bool confirm(bool default_yes = true);
  int choice(const std::vector<std::string>& options, int default_choice = 0);
  std::string input(const std::string& placeholder = "");

  // ── Configuration ──
  Prompt& style(Style s);
  Prompt& colors(bool enabled);
  Prompt& force_yes(bool yes);  // For --yes flag
  Prompt& force_no(bool no);    // For --no flag

  // ── Auto-populate from context ──
  void load_context(const nazg::directive::context* ctx);

private:
  nazg::blackbox::logger* log_;
  PromptContext context_;
  PromptContent content_;
  Style style_ = Style::STANDARD;

  std::optional<bool> force_answer_;  // For --yes/--no flags
  std::optional<bool> colors_enabled_;
  bool unicode_enabled_ = true;

  ColorFormatter colors_;
  IconSet icons_;

  // Terminal detection
  bool should_use_colors() const;
  bool is_interactive() const;

  // Rendering
  void render_header();
  void render_body();
  void render_footer_confirm(bool default_yes);
  void render_footer_choice(const std::vector<std::string>& options, int default_choice);
  void render_footer_input(const std::string& placeholder);

  // Input reading
  bool read_confirmation(bool default_yes);
  int read_choice(size_t num_options, int default_choice);
  std::string read_line(const std::string& placeholder);

  // Box drawing
  void print_box_top(int width);
  void print_box_line(const std::string& content, int width);
  void print_box_separator(int width);
  void print_box_bottom(int width);

  int terminal_width() const;
};

// ── Quick Helpers ──
bool confirm(const std::string& question, bool default_yes = true);
int choose(const std::string& question,
           const std::vector<std::string>& options,
           int default_choice = 0);
std::string ask(const std::string& question,
                const std::string& placeholder = "");

} // namespace nazg::prompt

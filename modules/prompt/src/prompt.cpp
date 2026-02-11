// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 purpleneutral
//
// This file is part of nazg.
//
// nazg is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// nazg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with nazg. If not, see <https://www.gnu.org/licenses/>.

#include "prompt/prompt.hpp"
#include "blackbox/logger.hpp"
#include "directive/context.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace nazg::prompt {

// ── Constructor ──

Prompt::Prompt(nazg::blackbox::logger* log)
    : log_(log),
      colors_(Terminal::supports_color()),
      icons_(true) {}

// ── Context Builders ──

Prompt& Prompt::title(const std::string& command_name) {
  context_.title = command_name;
  return *this;
}

Prompt& Prompt::project(const std::string& name, const std::string& path) {
  context_.project_name = name;
  context_.project_path = path;
  return *this;
}

Prompt& Prompt::fact(const std::string& key, const std::string& value) {
  context_.facts.push_back({key, value});
  return *this;
}

Prompt& Prompt::status(const std::string& message) {
  context_.status_message = message;
  return *this;
}

Prompt& Prompt::timestamp(bool show) {
  context_.show_timestamp = show;
  return *this;
}

// ── Content Builders ──

Prompt& Prompt::question(const std::string& text) {
  content_.question = text;
  return *this;
}

Prompt& Prompt::action(const std::string& description) {
  content_.actions.push_back(description);
  return *this;
}

Prompt& Prompt::warning(const std::string& text) {
  content_.warnings.push_back(text);
  return *this;
}

Prompt& Prompt::info(const std::string& text) {
  content_.infos.push_back(text);
  return *this;
}

Prompt& Prompt::detail(const std::string& text) {
  content_.details.push_back(text);
  return *this;
}

// ── Configuration ──

Prompt& Prompt::style(Style s) {
  style_ = s;
  return *this;
}

Prompt& Prompt::colors(bool enabled) {
  colors_enabled_ = enabled;
  return *this;
}

Prompt& Prompt::force_yes(bool yes) {
  if (yes) {
    force_answer_ = true;
  }
  return *this;
}

Prompt& Prompt::force_no(bool no) {
  if (no) {
    force_answer_ = false;
  }
  return *this;
}

void Prompt::load_context(const nazg::directive::context* ctx) {
  if (!ctx) return;

  // TODO: Auto-populate from context when we have project info
  // For now, caller must manually set context via builder methods
}

// ── Terminal Detection ──

bool Prompt::should_use_colors() const {
  if (colors_enabled_.has_value()) {
    return *colors_enabled_;
  }
  return Terminal::supports_color();
}

bool Prompt::is_interactive() const {
  return Terminal::is_interactive();
}

int Prompt::terminal_width() const {
  int width = Terminal::width();
  return std::min(width, 100);  // Cap at 100 for readability
}

// ── Rendering ──

void Prompt::render_header() {
  if (style_ == Style::MINIMAL) {
    // Minimal: nazg [command] • ...
    std::cout << colors_.cyan("nazg");
    if (!context_.title.empty()) {
      std::cout << " " << colors_.dim("[" + context_.title + "]");
    }
    std::cout << " " << colors_.dim("•") << " ";
    return;
  }

  // Standard/Verbose: Box header
  int width = terminal_width();
  std::string header = "nazg";
  if (!context_.title.empty()) {
    header += " • " + context_.title;
  }

  print_box_top(width);
  print_box_line(colors_.bold(header), width);

  // Show project context
  if (!context_.project_name.empty() || !context_.project_path.empty()) {
    std::string project_line = "Project: ";
    if (!context_.project_name.empty()) {
      project_line += context_.project_name;
    }
    if (!context_.project_path.empty()) {
      if (!context_.project_name.empty()) {
        project_line += " ";
      }
      project_line += colors_.gray("(" + context_.project_path + ")");
    }
    print_box_line(project_line, width);
  }

  // Show facts
  for (const auto& [key, value] : context_.facts) {
    std::string fact_line = colors_.blue(key + ":") + " " + value;
    print_box_line(fact_line, width);
  }

  // Show status
  if (!context_.status_message.empty()) {
    print_box_line(colors_.gray("Status: " + context_.status_message), width);
  }
}

void Prompt::render_body() {
  if (style_ == Style::MINIMAL) {
    // Minimal: question on same line as header
    std::cout << content_.question << "\n";

    // Actions
    for (const auto& action : content_.actions) {
      std::cout << "  " << icons_.bullet() << " " << action << "\n";
    }
    return;
  }

  // Standard/Verbose: Box body
  int width = terminal_width();

  print_box_separator(width);

  // Question
  if (!content_.question.empty()) {
    print_box_line(content_.question, width);
    if (!content_.actions.empty() || !content_.warnings.empty() || !content_.infos.empty()) {
      print_box_line("", width);
    }
  }

  // Actions
  for (const auto& action : content_.actions) {
    std::string line = "  " + icons_.bullet() + " " + action;
    print_box_line(line, width);
  }

  // Warnings
  for (const auto& warning : content_.warnings) {
    std::string line = "  " + colors_.yellow(icons_.warning()) + " " + warning;
    print_box_line(line, width);
  }

  // Infos
  for (const auto& info_text : content_.infos) {
    std::string line = "  " + colors_.cyan(icons_.info()) + " " + info_text;
    print_box_line(line, width);
  }

  // Details
  for (const auto& detail : content_.details) {
    print_box_line(colors_.gray(detail), width);
  }
}

void Prompt::render_footer_confirm(bool default_yes) {
  if (style_ == Style::MINIMAL) {
    std::cout << (default_yes ? "(Y/n) " : "(y/N) ");
    std::cout.flush();
    return;
  }

  int width = terminal_width();
  std::string prompt_text = default_yes ? "(Y/n)" : "(y/N)";
  print_box_line("", width);
  print_box_bottom(width);
  std::cout << colors_.bold(icons_.arrow() + "> ") << prompt_text << " ";
  std::cout.flush();
}

void Prompt::render_footer_choice(const std::vector<std::string>& options, int default_choice) {
  if (style_ == Style::MINIMAL) {
    std::cout << "Select [1-" << options.size() << "]: ";
    std::cout.flush();
    return;
  }

  int width = terminal_width();
  print_box_line("", width);
  print_box_bottom(width);
  std::cout << colors_.bold(icons_.arrow() + "> ");
  std::cout << "[1-" << options.size() << "] ";
  if (default_choice >= 0 && default_choice < static_cast<int>(options.size())) {
    std::cout << colors_.gray("(default: " + std::to_string(default_choice + 1) + ")");
  }
  std::cout << " ";
  std::cout.flush();
}

void Prompt::render_footer_input(const std::string& placeholder) {
  if (style_ == Style::MINIMAL) {
    if (!placeholder.empty()) {
      std::cout << colors_.gray("[" + placeholder + "]") << " ";
    }
    std::cout << "> ";
    std::cout.flush();
    return;
  }

  int width = terminal_width();
  print_box_line("", width);
  print_box_bottom(width);
  std::cout << colors_.bold(icons_.arrow() + "> ");
  if (!placeholder.empty()) {
    std::cout << colors_.gray("[" + placeholder + "]") << " ";
  }
  std::cout.flush();
}

// ── Box Drawing ──

void Prompt::print_box_top(int width) {
  std::cout << "┌─";
  for (int i = 2; i < width; ++i) {
    std::cout << "─";
  }
  std::cout << "\n";
}

void Prompt::print_box_line(const std::string& content, int /*width*/) {
  // Note: content may contain ANSI color codes, so we can't just use content.length()
  // For now, we'll just print it. TODO: Handle ANSI codes properly for padding.
  std::cout << "│ " << content << "\n";
}

void Prompt::print_box_separator(int width) {
  std::cout << "├─";
  for (int i = 2; i < width; ++i) {
    std::cout << "─";
  }
  std::cout << "\n";
}

void Prompt::print_box_bottom(int /*width*/) {
  std::cout << "└─> ";
}

// ── Input Reading ──

bool Prompt::read_confirmation(bool default_yes) {
  std::string input;
  std::getline(std::cin, input);

  if (input.empty()) {
    return default_yes;
  }

  char first = std::tolower(input[0]);
  return first == 'y';
}

int Prompt::read_choice(size_t num_options, int default_choice) {
  std::string input;
  std::getline(std::cin, input);

  if (input.empty() && default_choice >= 0) {
    return default_choice;
  }

  try {
    int choice = std::stoi(input);
    if (choice >= 1 && choice <= static_cast<int>(num_options)) {
      return choice - 1;  // Convert to 0-indexed
    }
  } catch (...) {
    // Invalid input
  }

  return -1;  // Invalid
}

std::string Prompt::read_line(const std::string& /*placeholder*/) {
  std::string input;
  std::getline(std::cin, input);
  return input;
}

// ── Public API ──

bool Prompt::confirm(bool default_yes) {
  // Check for forced answer (--yes / --no flags)
  if (force_answer_.has_value()) {
    if (log_) {
      log_->debug("Prompt", "Using forced answer: " +
                  std::string(*force_answer_ ? "yes" : "no"));
    }
    return *force_answer_;
  }

  // Check for non-interactive mode
  if (!is_interactive()) {
    const char* noninteractive = std::getenv("NAZG_NONINTERACTIVE");
    if (noninteractive) {
      if (log_) {
        log_->warn("Prompt", "Non-interactive mode: using default " +
                   std::string(default_yes ? "yes" : "no"));
      }
      return default_yes;
    }

    if (log_) {
      log_->error("Prompt", "stdin not a TTY and NAZG_NONINTERACTIVE not set");
    }
    std::cerr << "Error: Cannot prompt in non-interactive mode\n";
    std::cerr << "Hint: Use --yes flag or set NAZG_NONINTERACTIVE=1\n";
    return default_yes;
  }

  // Render and prompt
  render_header();
  render_body();
  render_footer_confirm(default_yes);

  return read_confirmation(default_yes);
}

int Prompt::choice(const std::vector<std::string>& options, int default_choice) {
  if (options.empty()) {
    return -1;
  }

  // Check for forced answer
  if (force_answer_.has_value()) {
    return default_choice >= 0 ? default_choice : 0;
  }

  // Check for non-interactive mode
  if (!is_interactive()) {
    if (log_) {
      log_->warn("Prompt", "Non-interactive mode: using default choice");
    }
    return default_choice >= 0 ? default_choice : 0;
  }

  // Render header and question
  render_header();
  if (!content_.question.empty()) {
    if (style_ == Style::MINIMAL) {
      std::cout << content_.question << "\n";
    } else {
      int width = terminal_width();
      print_box_separator(width);
      print_box_line(content_.question, width);
      print_box_line("", width);
    }
  }

  // Render options
  for (size_t i = 0; i < options.size(); ++i) {
    std::string line = "  " + std::to_string(i + 1) + ". " + options[i];
    if (style_ == Style::MINIMAL) {
      std::cout << line << "\n";
    } else {
      int width = terminal_width();
      print_box_line(line, width);
    }
  }

  render_footer_choice(options, default_choice);

  int result = read_choice(options.size(), default_choice);
  if (result < 0) {
    std::cerr << "Invalid choice\n";
    return default_choice >= 0 ? default_choice : 0;
  }

  return result;
}

std::string Prompt::input(const std::string& placeholder) {
  // Check for non-interactive mode
  if (!is_interactive()) {
    if (log_) {
      log_->warn("Prompt", "Non-interactive mode: returning empty string");
    }
    return "";
  }

  render_header();
  render_body();
  render_footer_input(placeholder);

  return read_line(placeholder);
}

// ── Quick Helpers ──

bool confirm(const std::string& question, bool default_yes) {
  Prompt p;
  p.question(question);
  return p.confirm(default_yes);
}

int choose(const std::string& question,
           const std::vector<std::string>& options,
           int default_choice) {
  Prompt p;
  p.question(question);
  return p.choice(options, default_choice);
}

std::string ask(const std::string& question, const std::string& placeholder) {
  Prompt p;
  p.question(question);
  return p.input(placeholder);
}

} // namespace nazg::prompt

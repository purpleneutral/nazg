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

#pragma once
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

#include "blackbox/level.hpp"
#include "blackbox/options.hpp"

#ifndef NAZG_LOG_DEFAULT_TAG
#define NAZG_LOG_DEFAULT_TAG "nazg"
#endif

namespace nazg::blackbox {

class logger {
public:
  // Construct with inline options. (You can also call configure() later.)
  explicit logger(const options &opts = options{});
  ~logger();

  // Configuration
  void configure(const options &cfg);
  void set_level(level lvl);
  level get_level() const;

  void set_color(bool on);
  void set_tag_width(int w);
  void enable_tui_mode(bool on = true);
  bool tui_mode() const;

  void set_console_enabled(bool on);
  bool console_enabled() const;

  // Convenience methods
  void debug(const std::string &tag, const std::string &msg);
  void info(const std::string &tag, const std::string &msg);
  void succ(const std::string &tag, const std::string &msg);
  void warn(const std::string &tag, const std::string &msg);
  void error(const std::string &tag, const std::string &msg);

  // Lower-level
  void submit(level lvl, const std::string &msg, const char *file, int line,
              const char *func, const char *tag);

private:
  void open_file_if_needed();
  void maybe_rotate();
  std::string format_prefix(level lvl, const char *file, int line,
                            const char *func, const char *tag,
                            bool for_file_sink) const;

  static const char *level_name(level lvl);
  static const char *level_color(level lvl);

  static std::string now_string(bool include_ms);
  static std::string today_string(); // YYYY-MM-DD
  static std::string basename_of(const char *path);

  // Daily rollover helpers (require m_ held)
  void roll_to_today_unlocked();
  std::filesystem::path make_dated_path_unlocked(const std::string &date) const;

private:
  mutable std::mutex m_;
  options cfg_{};
  std::ofstream fout_;
  std::atomic<level> level_{level::INFO};
  bool tui_mode_ = false;

  // For dated log naming/rotation
  std::filesystem::path resolved_dir_; // directory where logs go
  std::string base_name_;              // e.g. "nazg"
  std::string ext_;                    // e.g. ".log"
  std::string current_date_;           // YYYY-MM-DD currently open
  std::filesystem::path current_path_; // full path to current log file
};

} // namespace nazg::blackbox

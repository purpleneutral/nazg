#pragma once
#include "blackbox/level.hpp"
#include <string>

namespace nazg::blackbox {

enum class source_style { none = 0, basename, full };

// Parse source_style from string (case-insensitive)
// Returns default_style if string doesn't match
source_style parse_source_style(const std::string &str,
                                 source_style default_style = source_style::basename);

struct options {
  level min_level = level::INFO;
  bool console_enabled = false;  // Silent by default, enable with --verbose or NAZG_LOG_CONSOLE=1
  bool console_colors = true;
  bool color_in_file = false;

  // e.g., "nazg.log" → resolved to nazg-YYYY-MM-DD.log in a log dir
  std::string file_path = "nazg.log";

  std::size_t rotate_bytes = 5 * 1024 * 1024; // 5 MB
  int rotate_files = 3;
  bool async_mode = false;

  source_style src_style = source_style::basename;
  bool include_ms = true;
  bool pad_level = true;
  int tag_width = 12;
  bool include_tid = true;
  bool include_pid = false;
};

} // namespace nazg::blackbox

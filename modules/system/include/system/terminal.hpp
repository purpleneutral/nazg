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
#include <string>

namespace nazg::system {

// Terminal color support levels
enum class ColorSupport {
  NONE,      // No color support
  ANSI_8,    // 8 basic colors (30-37)
  ANSI_16,   // 16 colors (bold variants)
  ANSI_256,  // 256 color palette
  TRUE_COLOR // 24-bit RGB (16 million colors)
};

// Terminal capabilities
struct TerminalCapabilities {
  bool is_tty = false;
  bool supports_unicode = false;
  ColorSupport color_support = ColorSupport::NONE;
  int width = 80;
  int height = 24;
};

// Get terminal width in columns
int term_width();

// Get terminal height in rows
int term_height();

// Check if stdout is connected to a TTY
bool is_tty();

// Check if stdin is connected to a TTY
bool is_interactive();

// Detect color support level
ColorSupport detect_color_support();

// Check if terminal supports Unicode
bool supports_unicode();

// Get all terminal capabilities at once (cached)
const TerminalCapabilities& get_capabilities();

// Force refresh capability detection
void refresh_capabilities();

// ANSI color codes (8/16 colors)
namespace ansi {
inline constexpr const char *reset = "\x1b[0m";
inline constexpr const char *bold = "\x1b[1m";
inline constexpr const char *dim = "\x1b[2m";
inline constexpr const char *italic = "\x1b[3m";
inline constexpr const char *underline = "\x1b[4m";

// Foreground colors (30-37)
inline constexpr const char *f_black = "\x1b[30m";
inline constexpr const char *f_red = "\x1b[31m";
inline constexpr const char *f_green = "\x1b[32m";
inline constexpr const char *f_yellow = "\x1b[33m";
inline constexpr const char *f_blue = "\x1b[34m";
inline constexpr const char *f_magenta = "\x1b[35m";
inline constexpr const char *f_cyan = "\x1b[36m";
inline constexpr const char *f_white = "\x1b[37m";

// Bright foreground colors (90-97)
inline constexpr const char *f_bright_black = "\x1b[90m";
inline constexpr const char *f_bright_red = "\x1b[91m";
inline constexpr const char *f_bright_green = "\x1b[92m";
inline constexpr const char *f_bright_yellow = "\x1b[93m";
inline constexpr const char *f_bright_blue = "\x1b[94m";
inline constexpr const char *f_bright_magenta = "\x1b[95m";
inline constexpr const char *f_bright_cyan = "\x1b[96m";
inline constexpr const char *f_bright_white = "\x1b[97m";

// Background colors (40-47)
inline constexpr const char *b_black = "\x1b[40m";
inline constexpr const char *b_red = "\x1b[41m";
inline constexpr const char *b_green = "\x1b[42m";
inline constexpr const char *b_yellow = "\x1b[43m";
inline constexpr const char *b_blue = "\x1b[44m";
inline constexpr const char *b_magenta = "\x1b[45m";
inline constexpr const char *b_cyan = "\x1b[46m";
inline constexpr const char *b_white = "\x1b[47m";
} // namespace ansi

// 256-color palette
std::string ansi_256_fg(int color);
std::string ansi_256_bg(int color);

// True color (24-bit RGB)
std::string ansi_rgb_fg(int r, int g, int b);
std::string ansi_rgb_bg(int r, int g, int b);

} // namespace nazg::system

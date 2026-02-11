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

#include "tui/layout_utils.hpp"

#include <algorithm>
#include <sstream>

namespace nazg::tui {

namespace {
ftxui::Element add_padding(ftxui::Element element, int padding) {
  if (padding <= 0) {
    return element;
  }

  auto horizontal_pad = ftxui::filler() |
                        ftxui::size(ftxui::WIDTH, ftxui::EQUAL, padding);
  auto vertical_pad = ftxui::filler() |
                      ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, padding);

  return ftxui::hbox({
      horizontal_pad,
      ftxui::vbox({vertical_pad, std::move(element), vertical_pad}),
      horizontal_pad,
  });
}

} // namespace

int horizontal_overhead(const FrameOptions& options) {
  int border = options.border ? options.border_thickness * 2 : 0;
  return border + options.padding * 2;
}

int vertical_overhead(const FrameOptions& options) {
  int border = options.border ? options.border_thickness * 2 : 0;
  return border + options.padding * 2;
}

ftxui::Element apply_frame(ftxui::Element inner,
                           int viewport_width,
                           int viewport_height,
                           const Theme& theme,
                           const FrameOptions& options) {
  if (!inner) {
    inner = ftxui::text("");
  }

  const int clamp_width = std::max(
      0, viewport_width - horizontal_overhead(options));
  const int clamp_height = std::max(
      0, viewport_height - vertical_overhead(options));

  if (options.clamp_to_viewport) {
    if (clamp_width > 0) {
      inner = inner | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, clamp_width);
    }
    if (clamp_height > 0) {
      inner = inner | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, clamp_height);
    }
  }

  inner = inner | ftxui::xflex | ftxui::yflex;
  inner = add_padding(std::move(inner), options.padding);

  if (options.border) {
    inner = inner | ftxui::borderStyled(theme.active_border);
  }

  if (viewport_width > 0) {
    inner = inner | ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, viewport_width);
  }
  if (viewport_height > 0) {
    inner = inner | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, viewport_height);
  }

  return inner | ftxui::xflex | ftxui::yflex;
}

std::vector<std::string> wrap_text(const std::string& text, int max_width) {
  if (max_width <= 0) {
    return {text};
  }

  std::vector<std::string> lines;
  std::string segment;
  std::istringstream paragraph_stream(text);

  while (std::getline(paragraph_stream, segment, '\n')) {
    if (segment.empty()) {
      lines.emplace_back();
      continue;
    }

    std::istringstream word_stream(segment);
    std::string word;
    std::string current_line;

    while (word_stream >> word) {
      if (static_cast<int>(word.size()) >= max_width) {
        if (!current_line.empty()) {
          lines.push_back(current_line);
          current_line.clear();
        }

        std::size_t pos = 0;
        while (pos < word.size()) {
          auto chunk = word.substr(pos, static_cast<std::size_t>(max_width));
          lines.push_back(chunk);
          pos += static_cast<std::size_t>(max_width);
        }
        continue;
      }

      if (current_line.empty()) {
        current_line = word;
      } else if (static_cast<int>(current_line.size() + 1 + word.size()) <=
                 max_width) {
        current_line += ' ';
        current_line += word;
      } else {
        lines.push_back(current_line);
        current_line = word;
      }
    }

    if (!current_line.empty()) {
      lines.push_back(current_line);
    }
  }

  if (lines.empty()) {
    lines.emplace_back();
  }

  return lines;
}

ftxui::Element make_wrapped_block(const std::string& text, int max_width) {
  auto wrapped_lines = wrap_text(text, max_width);
  ftxui::Elements elements;
  elements.reserve(wrapped_lines.size());
  for (const auto& line : wrapped_lines) {
    elements.push_back(ftxui::text(line));
  }
  return ftxui::vbox(std::move(elements));
}

const FrameOptions& default_menu_frame_options() {
  static FrameOptions options;
  return options;
}

} // namespace nazg::tui


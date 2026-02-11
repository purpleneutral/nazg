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

#include "tui/theme.hpp"
#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

namespace nazg::tui {

struct FrameOptions {
  int padding = 1;
  int border_thickness = 1;
  bool border = true;
  bool clamp_to_viewport = true;
};

int horizontal_overhead(const FrameOptions& options);
int vertical_overhead(const FrameOptions& options);

ftxui::Element apply_frame(ftxui::Element inner,
                           int viewport_width,
                           int viewport_height,
                           const Theme& theme,
                           const FrameOptions& options = {});

std::vector<std::string> wrap_text(const std::string& text, int max_width);
ftxui::Element make_wrapped_block(const std::string& text, int max_width);

const FrameOptions& default_menu_frame_options();

} // namespace nazg::tui


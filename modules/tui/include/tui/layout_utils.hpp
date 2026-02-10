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


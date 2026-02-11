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
#include "prompt/colors.hpp"
#include <string>
#include <vector>

namespace nazg::prompt {

// Forward declaration
class ColorFormatter;

// Gradient generator for smooth color transitions
class Gradient {
public:
  // Create gradient between two colors
  Gradient(const Color& start, const Color& end, int steps);
  Gradient(const std::string& start_hex, const std::string& end_hex, int steps);

  // Generate gradient colors
  std::vector<Color> colors() const;

  // Apply gradient to text (each character gets a color)
  std::string apply(const std::string& text, const ColorFormatter& fmt) const;

  // Apply gradient to a string (distributed across the string)
  std::string apply_smooth(const std::string& text, const ColorFormatter& fmt) const;

  // Generate progress bar gradient (left to right)
  std::string progress_bar(int width, float percentage, const ColorFormatter& fmt) const;

  // ── Preset Gradients ──
  static Gradient rainbow(int steps = 7);
  static Gradient fire(int steps = 5);
  static Gradient ocean(int steps = 5);
  static Gradient forest(int steps = 5);
  static Gradient sunset(int steps = 5);
  static Gradient purple_blue(int steps = 5);
  static Gradient cyan_green(int steps = 5);

  // Nord theme gradients
  static Gradient nord_frost(int steps = 4);
  static Gradient nord_aurora(int steps = 5);

private:
  Color start_;
  Color end_;
  int steps_;

  // Interpolate between two colors (t = 0.0 to 1.0)
  Color interpolate(float t) const;

  // Linear interpolation helper
  int lerp(int a, int b, float t) const;
};

} // namespace nazg::prompt

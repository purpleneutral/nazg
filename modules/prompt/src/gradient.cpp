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

#include "prompt/gradient.hpp"
#include "prompt/colors.hpp"
#include <algorithm>
#include <cmath>

namespace nazg::prompt {

// ── Constructors ──

Gradient::Gradient(const Color& start, const Color& end, int steps)
    : start_(start), end_(end), steps_(std::max(2, steps)) {}

Gradient::Gradient(const std::string& start_hex, const std::string& end_hex, int steps)
    : start_(Color::from_hex(start_hex)),
      end_(Color::from_hex(end_hex)),
      steps_(std::max(2, steps)) {}

// ── Color Generation ──

std::vector<Color> Gradient::colors() const {
  std::vector<Color> result;
  result.reserve(steps_);

  for (int i = 0; i < steps_; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(steps_ - 1);
    result.push_back(interpolate(t));
  }

  return result;
}

// ── Text Application ──

std::string Gradient::apply(const std::string& text, const ColorFormatter& fmt) const {
  if (text.empty()) {
    return text;
  }

  // Generate colors
  auto gradient_colors = colors();

  // Apply each color to corresponding character
  std::string result;
  size_t color_idx = 0;

  for (char c : text) {
    const Color& color = gradient_colors[color_idx % gradient_colors.size()];
    result += fmt.rgb(color, std::string(1, c));
    color_idx++;
  }

  return result;
}

std::string Gradient::apply_smooth(const std::string& text, const ColorFormatter& fmt) const {
  if (text.empty()) {
    return text;
  }

  size_t len = text.length();
  if (len == 1) {
    return fmt.rgb(start_, text);
  }

  std::string result;

  for (size_t i = 0; i < len; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(len - 1);
    Color color = interpolate(t);
    result += fmt.rgb(color, std::string(1, text[i]));
  }

  return result;
}

// ── Progress Bar ──

std::string Gradient::progress_bar(int width, float percentage, const ColorFormatter& fmt) const {
  if (width <= 0) {
    return "";
  }

  percentage = std::clamp(percentage, 0.0f, 1.0f);
  int filled = static_cast<int>(width * percentage);

  std::string bar;
  for (int i = 0; i < width; ++i) {
    const char* c = (i < filled) ? "█" : "░";
    float t = static_cast<float>(i) / static_cast<float>(width - 1);
    Color color = interpolate(t);
    bar += fmt.rgb(color, std::string(c));
  }

  return bar;
}

// ── Preset Gradients ──

Gradient Gradient::rainbow(int steps) {
  // Start with red, we'll do multi-step through apply
  // For now, simplified to cyan → magenta
  return Gradient(
    Color(0, 255, 255),    // Cyan
    Color(255, 0, 255),    // Magenta
    steps
  );
}

Gradient Gradient::fire(int steps) {
  return Gradient(
    Color(255, 0, 0),      // Red
    Color(255, 255, 0),    // Yellow
    steps
  );
}

Gradient Gradient::ocean(int steps) {
  return Gradient(
    Color(0, 105, 148),    // Deep blue
    Color(64, 224, 208),   // Turquoise
    steps
  );
}

Gradient Gradient::forest(int steps) {
  return Gradient(
    Color(34, 139, 34),    // Forest green
    Color(154, 205, 50),   // Yellow green
    steps
  );
}

Gradient Gradient::sunset(int steps) {
  return Gradient(
    Color(255, 94, 77),    // Coral
    Color(255, 176, 59),   // Orange
    steps
  );
}

Gradient Gradient::purple_blue(int steps) {
  return Gradient(
    Color(138, 43, 226),   // Blue violet
    Color(65, 105, 225),   // Royal blue
    steps
  );
}

Gradient Gradient::cyan_green(int steps) {
  return Gradient(
    Color(0, 255, 255),    // Cyan
    Color(0, 255, 127),    // Spring green
    steps
  );
}

// Nord theme gradients
Gradient Gradient::nord_frost(int steps) {
  return Gradient(
    Color(143, 188, 187),  // nord7
    Color(136, 192, 208),  // nord8
    steps
  );
}

Gradient Gradient::nord_aurora(int steps) {
  return Gradient(
    Color(191, 97, 106),   // nord11
    Color(235, 203, 139),  // nord13
    steps
  );
}

// ── Private Methods ──

Color Gradient::interpolate(float t) const {
  t = std::clamp(t, 0.0f, 1.0f);

  int r = lerp(start_.r, end_.r, t);
  int g = lerp(start_.g, end_.g, t);
  int b = lerp(start_.b, end_.b, t);

  return Color(r, g, b);
}

int Gradient::lerp(int a, int b, float t) const {
  return static_cast<int>(a + t * (b - a));
}

} // namespace nazg::prompt

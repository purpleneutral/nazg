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

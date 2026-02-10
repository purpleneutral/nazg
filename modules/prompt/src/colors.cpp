#include "prompt/colors.hpp"
#include "system/terminal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <regex>
#include <sstream>
#include <unistd.h>
#include <sys/ioctl.h>

namespace nazg::prompt {

// ── Color struct implementation ──

Color Color::from_hex(const std::string& hex) {
  std::string clean = hex;

  // Remove '#' if present
  if (!clean.empty() && clean[0] == '#') {
    clean = clean.substr(1);
  }

  // Must be 6 characters
  if (clean.length() != 6) {
    return Color(0, 0, 0);  // Invalid, return black
  }

  try {
    int r = std::stoi(clean.substr(0, 2), nullptr, 16);
    int g = std::stoi(clean.substr(2, 2), nullptr, 16);
    int b = std::stoi(clean.substr(4, 2), nullptr, 16);
    return Color(r, g, b);
  } catch (...) {
    return Color(0, 0, 0);  // Invalid, return black
  }
}

std::string Color::to_hex() const {
  std::ostringstream oss;
  oss << "#"
      << std::hex << std::setfill('0') << std::setw(2) << (r & 0xFF)
      << std::hex << std::setfill('0') << std::setw(2) << (g & 0xFF)
      << std::hex << std::setfill('0') << std::setw(2) << (b & 0xFF);
  return oss.str();
}

// ── Terminal (legacy - delegates to system::terminal) ──

bool Terminal::check_env_for_color() {
  return nazg::system::detect_color_support() != nazg::system::ColorSupport::NONE;
}

bool Terminal::supports_color() {
  return nazg::system::detect_color_support() != nazg::system::ColorSupport::NONE;
}

bool Terminal::is_interactive() {
  return nazg::system::is_interactive();
}

int Terminal::width() {
  return nazg::system::term_width();
}

int Terminal::height() {
  return nazg::system::term_height();
}

// ── ColorFormatter implementation ──

ColorFormatter::ColorFormatter(bool enable_colors)
    : enabled_(enable_colors) {
  if (enabled_) {
    capability_ = nazg::system::detect_color_support();
  } else {
    capability_ = nazg::system::ColorSupport::NONE;
  }
}

ColorFormatter::ColorFormatter(const nazg::system::TerminalCapabilities& caps)
    : enabled_(caps.color_support != nazg::system::ColorSupport::NONE),
      capability_(caps.color_support) {}

// ── RGB Color Methods ──

std::string ColorFormatter::rgb(int r, int g, int b, const std::string& text) const {
  return apply_rgb(r, g, b, false, text);
}

std::string ColorFormatter::rgb_bg(int r, int g, int b, const std::string& text) const {
  return apply_rgb(r, g, b, true, text);
}

std::string ColorFormatter::rgb(const Color& color, const std::string& text) const {
  return apply_rgb(color.r, color.g, color.b, false, text);
}

std::string ColorFormatter::rgb_bg(const Color& color, const std::string& text) const {
  return apply_rgb(color.r, color.g, color.b, true, text);
}

// ── Hex Color Methods ──

std::string ColorFormatter::hex(const std::string& hex_color, const std::string& text) const {
  Color c = Color::from_hex(hex_color);
  return rgb(c, text);
}

std::string ColorFormatter::hex_bg(const std::string& hex_color, const std::string& text) const {
  Color c = Color::from_hex(hex_color);
  return rgb_bg(c, text);
}

// ── 256-Color Methods ──

std::string ColorFormatter::c256(int color, const std::string& text) const {
  if (!enabled_ || text.empty()) {
    return text;
  }

  // If terminal doesn't support 256 colors, convert to RGB and degrade
  if (capability_ < nazg::system::ColorSupport::ANSI_256) {
    // Convert 256-color to approximate RGB, then degrade
    // This is simplified - a full 256 palette would need lookup table
    int r = ((color >> 5) & 0x07) * 255 / 7;
    int g = ((color >> 2) & 0x07) * 255 / 7;
    int b = (color & 0x03) * 255 / 3;
    return apply_rgb(r, g, b, false, text);
  }

  return nazg::system::ansi_256_fg(color & 0xFF) + text + color::RESET;
}

std::string ColorFormatter::c256_bg(int color, const std::string& text) const {
  if (!enabled_ || text.empty()) {
    return text;
  }

  if (capability_ < nazg::system::ColorSupport::ANSI_256) {
    int r = ((color >> 5) & 0x07) * 255 / 7;
    int g = ((color >> 2) & 0x07) * 255 / 7;
    int b = (color & 0x03) * 255 / 3;
    return apply_rgb(r, g, b, true, text);
  }

  return nazg::system::ansi_256_bg(color & 0xFF) + text + color::RESET;
}

// ── Basic 16 Color Methods (legacy compatibility) ──

std::string ColorFormatter::green(const std::string& text) const {
  return wrap(color::GREEN, text);
}

std::string ColorFormatter::red(const std::string& text) const {
  return wrap(color::RED, text);
}

std::string ColorFormatter::yellow(const std::string& text) const {
  return wrap(color::YELLOW, text);
}

std::string ColorFormatter::blue(const std::string& text) const {
  return wrap(color::BLUE, text);
}

std::string ColorFormatter::cyan(const std::string& text) const {
  return wrap(color::CYAN, text);
}

std::string ColorFormatter::magenta(const std::string& text) const {
  return wrap(color::MAGENTA, text);
}

std::string ColorFormatter::gray(const std::string& text) const {
  return wrap(color::GRAY, text);
}

std::string ColorFormatter::white(const std::string& text) const {
  return wrap(color::WHITE, text);
}

std::string ColorFormatter::bold(const std::string& text) const {
  return wrap(color::BOLD, text);
}

std::string ColorFormatter::dim(const std::string& text) const {
  return wrap(color::DIM, text);
}

// ── Utility Methods ──

size_t ColorFormatter::display_width(const std::string& text) {
  return strip_ansi(text).length();
}

std::string ColorFormatter::strip_ansi(const std::string& text) {
  // Match ANSI escape sequences: \x1b[...m or \033[...m
  static const std::regex ansi_regex("\x1b\\[[0-9;]*m");
  return std::regex_replace(text, ansi_regex, "");
}

bool ColorFormatter::has_ansi(const std::string& text) {
  return text.find("\x1b[") != std::string::npos ||
         text.find("\033[") != std::string::npos;
}

nazg::system::ColorSupport ColorFormatter::capability() const {
  return capability_;
}

// ── Private Methods ──

std::string ColorFormatter::wrap(const char* code, const std::string& text) const {
  if (!enabled_ || text.empty()) {
    return text;
  }
  return std::string(code) + text + color::RESET;
}

std::string ColorFormatter::apply_rgb(int r, int g, int b, bool bg, const std::string& text) const {
  if (!enabled_ || text.empty()) {
    return text;
  }

  // Clamp RGB values
  r = std::clamp(r, 0, 255);
  g = std::clamp(g, 0, 255);
  b = std::clamp(b, 0, 255);

  switch (capability_) {
    case nazg::system::ColorSupport::TRUE_COLOR: {
      // Use 24-bit RGB
      std::string code = bg ? nazg::system::ansi_rgb_bg(r, g, b)
                            : nazg::system::ansi_rgb_fg(r, g, b);
      return code + text + color::RESET;
    }

    case nazg::system::ColorSupport::ANSI_256: {
      // Convert to 256-color palette
      int color_256 = rgb_to_256(r, g, b);
      std::string code = bg ? nazg::system::ansi_256_bg(color_256)
                            : nazg::system::ansi_256_fg(color_256);
      return code + text + color::RESET;
    }

    case nazg::system::ColorSupport::ANSI_16:
    case nazg::system::ColorSupport::ANSI_8: {
      // Convert to 16-color ANSI
      int ansi16 = rgb_to_ansi16(r, g, b);
      const char* code;

      if (bg) {
        // Background colors (40-47)
        switch (ansi16) {
          case 0: code = "\033[40m"; break;  // Black
          case 1: code = "\033[41m"; break;  // Red
          case 2: code = "\033[42m"; break;  // Green
          case 3: code = "\033[43m"; break;  // Yellow
          case 4: code = "\033[44m"; break;  // Blue
          case 5: code = "\033[45m"; break;  // Magenta
          case 6: code = "\033[46m"; break;  // Cyan
          case 7: code = "\033[47m"; break;  // White
          default: code = "\033[40m"; break;
        }
      } else {
        // Foreground colors (30-37)
        switch (ansi16) {
          case 0: code = "\033[30m"; break;  // Black
          case 1: code = "\033[31m"; break;  // Red
          case 2: code = "\033[32m"; break;  // Green
          case 3: code = "\033[33m"; break;  // Yellow
          case 4: code = "\033[34m"; break;  // Blue
          case 5: code = "\033[35m"; break;  // Magenta
          case 6: code = "\033[36m"; break;  // Cyan
          case 7: code = "\033[37m"; break;  // White
          default: code = "\033[37m"; break;
        }
      }

      return std::string(code) + text + color::RESET;
    }

    default:
      return text;
  }
}

// Convert RGB to 256-color palette (0-255)
int ColorFormatter::rgb_to_256(int r, int g, int b) const {
  // Check if it's a grayscale color
  if (r == g && g == b) {
    if (r < 8) return 16;
    if (r > 248) return 231;
    return static_cast<int>(std::round(((r - 8) / 247.0) * 24.0)) + 232;
  }

  // Map to 6x6x6 color cube (16-231)
  int r_idx = static_cast<int>(std::round(r / 255.0 * 5.0));
  int g_idx = static_cast<int>(std::round(g / 255.0 * 5.0));
  int b_idx = static_cast<int>(std::round(b / 255.0 * 5.0));

  return 16 + (36 * r_idx) + (6 * g_idx) + b_idx;
}

// Convert RGB to ANSI 16-color (0-7)
int ColorFormatter::rgb_to_ansi16(int r, int g, int b) const {
  // Simple conversion based on which channel is dominant
  int luminance = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);

  // Check for near-grayscale
  int max_val = std::max({r, g, b});
  int min_val = std::min({r, g, b});

  if (max_val - min_val < 50) {
    // Grayscale: black or white
    return (luminance < 128) ? 0 : 7;
  }

  // Determine dominant color
  int color = 0;
  if (r > g && r > b) {
    color = 1;  // Red
  } else if (g > r && g > b) {
    color = 2;  // Green
  } else if (b > r && b > g) {
    color = 4;  // Blue
  } else if (r > 128 && g > 128) {
    color = 3;  // Yellow
  } else if (r > 128 && b > 128) {
    color = 5;  // Magenta
  } else if (g > 128 && b > 128) {
    color = 6;  // Cyan
  } else {
    color = 7;  // White
  }

  return color;
}

std::tuple<int, int, int> ColorFormatter::ansi16_to_rgb(int ansi16) const {
  // Convert ANSI 16 colors to approximate RGB
  switch (ansi16 & 0x7) {
    case 0: return {0, 0, 0};        // Black
    case 1: return {170, 0, 0};      // Red
    case 2: return {0, 170, 0};      // Green
    case 3: return {170, 85, 0};     // Yellow
    case 4: return {0, 0, 170};      // Blue
    case 5: return {170, 0, 170};    // Magenta
    case 6: return {0, 170, 170};    // Cyan
    case 7: return {170, 170, 170};  // White
    default: return {0, 0, 0};
  }
}

} // namespace nazg::prompt

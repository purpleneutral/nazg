#pragma once
#include <string>
#include <tuple>

namespace nazg::system {
  enum class ColorSupport;
  struct TerminalCapabilities;
}

namespace nazg::prompt {

// RGB Color
struct Color {
  int r = 0;
  int g = 0;
  int b = 0;

  Color() = default;
  Color(int r_, int g_, int b_) : r(r_), g(g_), b(b_) {}

  // Create from hex string (#RRGGBB or RRGGBB)
  static Color from_hex(const std::string& hex);

  // Convert to hex string
  std::string to_hex() const;

  // Equality
  bool operator==(const Color& other) const {
    return r == other.r && g == other.g && b == other.b;
  }
};

// ANSI color codes (legacy - kept for compatibility)
namespace color {
  // Regular colors
  inline const char* BLACK = "\033[0;30m";
  inline const char* RED = "\033[0;31m";
  inline const char* GREEN = "\033[0;32m";
  inline const char* YELLOW = "\033[0;33m";
  inline const char* BLUE = "\033[0;34m";
  inline const char* MAGENTA = "\033[0;35m";
  inline const char* CYAN = "\033[0;36m";
  inline const char* WHITE = "\033[0;37m";
  inline const char* GRAY = "\033[0;90m";

  // Bold colors
  inline const char* BOLD_BLACK = "\033[1;30m";
  inline const char* BOLD_RED = "\033[1;31m";
  inline const char* BOLD_GREEN = "\033[1;32m";
  inline const char* BOLD_YELLOW = "\033[1;33m";
  inline const char* BOLD_BLUE = "\033[1;34m";
  inline const char* BOLD_MAGENTA = "\033[1;35m";
  inline const char* BOLD_CYAN = "\033[1;36m";
  inline const char* BOLD_WHITE = "\033[1;37m";

  // Special
  inline const char* RESET = "\033[0m";
  inline const char* BOLD = "\033[1m";
  inline const char* DIM = "\033[2m";
}

// Terminal capability detection (deprecated - use system::terminal)
class Terminal {
public:
  static bool supports_color();
  static bool is_interactive();
  static int width();
  static int height();

private:
  static bool check_env_for_color();
};

// Enhanced color formatter with RGB, 256-color, and auto-adaptation
class ColorFormatter {
public:
  // Constructor - auto-detects capabilities or uses explicit setting
  explicit ColorFormatter(bool enable_colors = true);
  explicit ColorFormatter(const nazg::system::TerminalCapabilities& caps);

  // ── RGB Colors (auto-adapts to terminal capabilities) ──
  std::string rgb(int r, int g, int b, const std::string& text) const;
  std::string rgb_bg(int r, int g, int b, const std::string& text) const;
  std::string rgb(const Color& color, const std::string& text) const;
  std::string rgb_bg(const Color& color, const std::string& text) const;

  // ── Hex Colors (#RRGGBB) ──
  std::string hex(const std::string& color, const std::string& text) const;
  std::string hex_bg(const std::string& color, const std::string& text) const;

  // ── 256-Color Palette (0-255) ──
  std::string c256(int color, const std::string& text) const;
  std::string c256_bg(int color, const std::string& text) const;

  // ── Basic 16 Colors (legacy, kept for compatibility) ──
  std::string green(const std::string& text) const;
  std::string red(const std::string& text) const;
  std::string yellow(const std::string& text) const;
  std::string blue(const std::string& text) const;
  std::string cyan(const std::string& text) const;
  std::string magenta(const std::string& text) const;
  std::string gray(const std::string& text) const;
  std::string white(const std::string& text) const;
  std::string bold(const std::string& text) const;
  std::string dim(const std::string& text) const;

  // ── Utility Functions ──

  // Calculate display width (strips ANSI codes)
  static size_t display_width(const std::string& text);

  // Strip all ANSI codes from string
  static std::string strip_ansi(const std::string& text);

  // Check if string contains ANSI codes
  static bool has_ansi(const std::string& text);

  // Get current capability level
  nazg::system::ColorSupport capability() const;

private:
  bool enabled_;
  nazg::system::ColorSupport capability_;

  // Internal color application
  std::string apply_rgb(int r, int g, int b, bool bg, const std::string& text) const;
  std::string wrap(const char* code, const std::string& text) const;

  // Color conversion for degradation
  int rgb_to_256(int r, int g, int b) const;
  int rgb_to_ansi16(int r, int g, int b) const;
  std::tuple<int, int, int> ansi16_to_rgb(int ansi16) const;
};

} // namespace nazg::prompt

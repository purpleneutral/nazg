#include "system/terminal.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace nazg::system {

namespace {
// Cached capabilities
TerminalCapabilities g_capabilities;
bool g_capabilities_initialized = false;

// Detect if environment variable contains a value
bool env_contains(const char* var_name, const char* substring) {
  const char* value = std::getenv(var_name);
  if (!value) return false;
  return std::string(value).find(substring) != std::string::npos;
}

// Detect Unicode support
bool detect_unicode_support() {
  // Check if NO_UNICODE is set
  if (std::getenv("NO_UNICODE")) {
    return false;
  }

  // Check LC_ALL, LC_CTYPE, LANG for UTF-8
  const char* lc_all = std::getenv("LC_ALL");
  const char* lc_ctype = std::getenv("LC_CTYPE");
  const char* lang = std::getenv("LANG");

  if ((lc_all && strstr(lc_all, "UTF-8")) ||
      (lc_ctype && strstr(lc_ctype, "UTF-8")) ||
      (lang && strstr(lang, "UTF-8"))) {
    return true;
  }

  // Modern terminals usually support UTF-8
  return true;
}

// Detect color support level
ColorSupport detect_color_level() {
  // Check NO_COLOR standard (overrides everything)
  if (std::getenv("NO_COLOR")) {
    return ColorSupport::NONE;
  }

  // Check FORCE_COLOR
  const char* force_color = std::getenv("FORCE_COLOR");
  if (force_color) {
    int level = atoi(force_color);
    if (level == 3) return ColorSupport::TRUE_COLOR;
    if (level == 2) return ColorSupport::ANSI_256;
    if (level == 1) return ColorSupport::ANSI_16;
  }

  // Check COLORTERM for truecolor/24bit
  const char* colorterm = std::getenv("COLORTERM");
  if (colorterm) {
    std::string ct(colorterm);
    if (ct == "truecolor" || ct == "24bit") {
      return ColorSupport::TRUE_COLOR;
    }
  }

  // Check TERM variable
  const char* term = std::getenv("TERM");
  if (!term || std::strcmp(term, "dumb") == 0) {
    return ColorSupport::NONE;
  }

  std::string term_str(term);

  // Check for 256 color support
  if (term_str.find("256color") != std::string::npos ||
      term_str.find("256") != std::string::npos) {
    return ColorSupport::ANSI_256;
  }

  // Check for truecolor capable terminals
  if (term_str.find("kitty") != std::string::npos ||
      term_str.find("alacritty") != std::string::npos ||
      term_str.find("wezterm") != std::string::npos ||
      term_str.find("iterm") != std::string::npos ||
      env_contains("TERM_PROGRAM", "iTerm") ||
      env_contains("TERM_PROGRAM", "vscode") ||
      env_contains("TERM_PROGRAM", "Hyper")) {
    return ColorSupport::TRUE_COLOR;
  }

  // Check for basic color support
  if (term_str.find("color") != std::string::npos ||
      term_str.find("xterm") != std::string::npos ||
      term_str.find("screen") != std::string::npos ||
      term_str.find("tmux") != std::string::npos ||
      term_str.find("linux") != std::string::npos) {
    // Most modern terminals support at least 256 colors
    return ColorSupport::ANSI_256;
  }

  // Fallback to basic 16 colors if we're on a TTY
  return ColorSupport::ANSI_16;
}

void initialize_capabilities() {
  if (g_capabilities_initialized) return;

  g_capabilities.is_tty = is_tty() && is_interactive();
  g_capabilities.width = term_width();
  g_capabilities.height = term_height();
  g_capabilities.supports_unicode = detect_unicode_support();
  g_capabilities.color_support = g_capabilities.is_tty ? detect_color_level() : ColorSupport::NONE;

  g_capabilities_initialized = true;
}

} // anonymous namespace

int term_width() {
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
  }
#else
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
    return w.ws_col;
  }
#endif

  // Fallback to COLUMNS env var
  const char *cols = std::getenv("COLUMNS");
  if (cols)
    return std::max(40, atoi(cols));

  return 80; // Default fallback
}

int term_height() {
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }
#else
  struct winsize w;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_row > 0) {
    return w.ws_row;
  }
#endif

  // Fallback to LINES env var
  const char *lines = std::getenv("LINES");
  if (lines)
    return std::max(10, atoi(lines));

  return 24; // Default fallback
}

bool is_tty() {
#if defined(_WIN32)
  return _isatty(_fileno(stdout)) != 0;
#else
  return isatty(STDOUT_FILENO) != 0;
#endif
}

bool is_interactive() {
#if defined(_WIN32)
  return _isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0;
#else
  return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
#endif
}

ColorSupport detect_color_support() {
  if (!g_capabilities_initialized) {
    initialize_capabilities();
  }
  return g_capabilities.color_support;
}

bool supports_unicode() {
  if (!g_capabilities_initialized) {
    initialize_capabilities();
  }
  return g_capabilities.supports_unicode;
}

const TerminalCapabilities& get_capabilities() {
  if (!g_capabilities_initialized) {
    initialize_capabilities();
  }
  return g_capabilities;
}

void refresh_capabilities() {
  g_capabilities_initialized = false;
  initialize_capabilities();
}

// 256-color palette
std::string ansi_256_fg(int color) {
  std::ostringstream oss;
  oss << "\x1b[38;5;" << (color & 0xFF) << "m";
  return oss.str();
}

std::string ansi_256_bg(int color) {
  std::ostringstream oss;
  oss << "\x1b[48;5;" << (color & 0xFF) << "m";
  return oss.str();
}

// True color (24-bit RGB)
std::string ansi_rgb_fg(int r, int g, int b) {
  std::ostringstream oss;
  oss << "\x1b[38;2;" << (r & 0xFF) << ";" << (g & 0xFF) << ";" << (b & 0xFF) << "m";
  return oss.str();
}

std::string ansi_rgb_bg(int r, int g, int b) {
  std::ostringstream oss;
  oss << "\x1b[48;2;" << (r & 0xFF) << ";" << (g & 0xFF) << ";" << (b & 0xFF) << "m";
  return oss.str();
}

} // namespace nazg::system

#include "blackbox/logger.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <process.h> // _getpid
#else
#include <unistd.h> // getpid
#endif

namespace fs = std::filesystem;
namespace nazg::blackbox {

// ---------- parse helpers ----------

level parse_level(const std::string &str, level default_level) {
  std::string upper = str;
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](unsigned char c) { return std::toupper(c); });

  if (upper == "TRACE")
    return level::TRACE;
  if (upper == "DEBUG")
    return level::DEBUG;
  if (upper == "INFO")
    return level::INFO;
  if (upper == "SUCCESS" || upper == "SUCC")
    return level::SUCCESS;
  if (upper == "WARN" || upper == "WARNING")
    return level::WARN;
  if (upper == "ERROR" || upper == "ERR")
    return level::ERROR;
  if (upper == "FATAL")
    return level::FATAL;
  if (upper == "OFF")
    return level::OFF;

  return default_level;
}

source_style parse_source_style(const std::string &str,
                                 source_style default_style) {
  std::string lower = str;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "none")
    return source_style::none;
  if (lower == "basename" || lower == "base")
    return source_style::basename;
  if (lower == "full" || lower == "fullpath")
    return source_style::full;

  return default_style;
}

// ---------- platform log dirs & path resolution ----------

static fs::path default_log_dir() {
#if defined(__APPLE__)
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / "Library" / "Logs" / "nazg" / "logs";
  }
  return fs::temp_directory_path() / "nazg" / "logs";
#else
  if (const char *xdg = std::getenv("XDG_STATE_HOME")) {
    return fs::path(xdg) / "nazg" / "logs";
  }
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".local" / "state" / "nazg" / "logs";
  }
  return fs::temp_directory_path() / "nazg" / "logs";
#endif
}

static fs::path resolve_log_path(const std::string &requested) {
  if (requested.empty())
    return {};
  bool force_cwd = false;
  if (const char *e = std::getenv("NAZG_LOG_CWD")) {
    std::string v = e;
    force_cwd = (v == "1" || v == "true" || v == "TRUE");
  }
  if (const char *e = std::getenv("NAZG_LOG_DIR")) {
    fs::path dir = fs::path(e);
    fs::path leaf = fs::path(requested).filename();
    return dir / leaf;
  }
  fs::path p(requested);
  if (p.is_absolute() || (!requested.empty() && requested[0] == '.'))
    return p;
  if (force_cwd)
    return fs::current_path() / p;
  return default_log_dir() / p;
}

// ---------- helpers (time, labels) ----------

std::string logger::now_string(bool include_ms) {
  using clock = std::chrono::system_clock;
  auto tp = clock::now();
  auto t = clock::to_time_t(tp);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
  if (include_ms) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  tp.time_since_epoch()) %
              1000;
    os << "." << std::setw(3) << std::setfill('0') << ms.count();
  }
  return os.str();
}

std::string logger::today_string() {
  using clock = std::chrono::system_clock;
  auto t = clock::to_time_t(clock::now());
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%d");
  return os.str();
}

std::string logger::basename_of(const char *path) {
  if (!path)
    return {};
  const char *s = path;
  const char *last = s;
  while (*s) {
    if (*s == '/' || *s == '\\')
      last = s + 1;
    ++s;
  }
  return std::string(last);
}

const char *logger::level_name(level lvl) {
  switch (lvl) {
  case level::TRACE:
    return "TRACE";
  case level::DEBUG:
    return "DEBUG";
  case level::INFO:
    return "INFO";
  case level::SUCCESS:
    return "SUCC";
  case level::WARN:
    return "WARN";
  case level::ERROR:
    return "ERROR";
  case level::FATAL:
    return "FATAL";
  default:
    return "OFF";
  }
}
const char *logger::level_color(level lvl) {
  switch (lvl) {
  case level::TRACE:
    return "\033[90m";
  case level::DEBUG:
    return "\033[36m";
  case level::INFO:
    return "\033[37m";
  case level::SUCCESS:
    return "\033[92m";
  case level::WARN:
    return "\033[93m";
  case level::ERROR:
    return "\033[91m";
  case level::FATAL:
    return "\033[95m";
  default:
    return "\033[0m";
  }
}

// ---------- ctor/config ----------

logger::logger(const options &opts) { configure(opts); }

logger::~logger() {
  std::lock_guard<std::mutex> lock(m_);
  if (fout_.is_open()) {
    fout_.flush();
    fout_.close();
  }
}

void logger::configure(const options &cfg) {
  std::lock_guard<std::mutex> lock(m_);
  cfg_ = cfg; // copy
  level_.store(cfg.min_level, std::memory_order_relaxed);

  try {
    // Resolve requested path & split into dir/base/ext
    fs::path req = resolve_log_path(cfg_.file_path);
    resolved_dir_ = req.parent_path();
    auto leaf = req.filename();
    std::string stem = leaf.stem().string();     // "nazg"
    std::string ext = leaf.extension().string(); // ".log" or ""
    if (stem.empty())
      stem = "nazg";
    if (ext.empty())
      ext = ".log";
    base_name_ = stem;
    ext_ = ext;

    std::error_code ec;
    fs::create_directories(resolved_dir_, ec);
    if (ec) {
      std::cerr << "Warning: Failed to create log directory " << resolved_dir_
                << ": " << ec.message() << "\n";
      // Continue anyway - file logging will be disabled
    }
    roll_to_today_unlocked(); // sets current_path_ and opens fout_
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Error configuring logger: " << e.what() << "\n";
    // Fall back to console-only logging
    cfg_.console_enabled = true;
    current_path_.clear();
  } catch (const std::exception &e) {
    std::cerr << "Unexpected error configuring logger: " << e.what() << "\n";
    cfg_.console_enabled = true;
    current_path_.clear();
  }
}

void logger::set_level(level lvl) {
  level_.store(lvl, std::memory_order_relaxed);
}
level logger::get_level() const {
  return level_.load(std::memory_order_relaxed);
}

void logger::set_color(bool on) {
  std::lock_guard<std::mutex> lk(m_);
  cfg_.console_colors = on;
}
void logger::set_tag_width(int w) {
  std::lock_guard<std::mutex> lk(m_);
  if (w > 0)
    cfg_.tag_width = w;
}
void logger::enable_tui_mode(bool on) {
  std::lock_guard<std::mutex> lk(m_);
  tui_mode_ = on;
}
bool logger::tui_mode() const {
  std::lock_guard<std::mutex> lk(m_);
  return tui_mode_;
}

void logger::set_console_enabled(bool on) {
  std::lock_guard<std::mutex> lk(m_);
  cfg_.console_enabled = on;
}
bool logger::console_enabled() const {
  std::lock_guard<std::mutex> lk(m_);
  return cfg_.console_enabled;
}

// ---------- daily roll + sinks & rotation ----------

fs::path logger::make_dated_path_unlocked(const std::string &date) const {
  return resolved_dir_ / fs::path(base_name_ + "-" + date + ext_);
}

void logger::roll_to_today_unlocked() {
  std::string today = today_string();
  if (today == current_date_ && fout_.is_open())
    return;

  if (fout_.is_open())
    fout_.close();
  current_date_ = today;
  current_path_ = make_dated_path_unlocked(current_date_);

  std::error_code ec;
  fs::create_directories(current_path_.parent_path(), ec);
  if (ec) {
    std::cerr << "Warning: Failed to create log directory "
              << current_path_.parent_path() << ": " << ec.message() << "\n";
  }
  open_file_if_needed();
}

void logger::open_file_if_needed() {
  if (!current_path_.empty() && !fout_.is_open()) {
    fout_.open(current_path_, std::ios::out | std::ios::app);
    if (!fout_.is_open() || !fout_.good()) {
      std::cerr << "Warning: Failed to open log file " << current_path_ << "\n";
      fout_.close();
    }
  }
}

void logger::maybe_rotate() {
  if (current_path_.empty() || cfg_.rotate_bytes == 0)
    return;

  std::error_code ec;
  auto sz = fs::file_size(current_path_, ec);
  if (ec) {
    // Can't check file size - might not exist yet
    return;
  }
  if (sz < cfg_.rotate_bytes)
    return;

  if (fout_.is_open())
    fout_.close();

  // Shift: file.(N-1)->.N ... file->.1
  for (int i = cfg_.rotate_files - 1; i >= 1; --i) {
    fs::path src = current_path_.string() + "." + std::to_string(i);
    fs::path dst = current_path_.string() + "." + std::to_string(i + 1);
    std::error_code ecMv;
    if (fs::exists(src, ecMv)) {
      fs::rename(src, dst, ecMv);
      if (ecMv) {
        std::cerr << "Warning: Failed to rotate " << src << " to " << dst
                  << ": " << ecMv.message() << "\n";
      }
    }
  }
  {
    std::error_code ecMv;
    fs::path rotated = current_path_.string() + ".1";
    fs::rename(current_path_, rotated, ecMv);
    if (ecMv) {
      std::cerr << "Warning: Failed to rotate " << current_path_ << " to "
                << rotated << ": " << ecMv.message() << "\n";
    }
  }
  open_file_if_needed();
}

// ---------- formatting ----------

std::string logger::format_prefix(level lvl, const char *file, int line,
                                  const char *func, const char *tag,
                                  bool /*for_file_sink*/) const {
  std::ostringstream os;

  // Time
  os << now_string(cfg_.include_ms) << " ";

  // Level
  const char *lname = level_name(lvl);
  if (cfg_.pad_level)
    os << std::left << std::setw(5) << std::setfill(' ') << lname << " ";
  else
    os << lname << " ";

  // Tag
  std::string tag_s = tag ? tag : NAZG_LOG_DEFAULT_TAG;
  if (cfg_.tag_width > 0)
    os << "[" << std::left << std::setw(cfg_.tag_width) << std::setfill(' ')
       << tag_s << "] ";
  else
    os << "[" << tag_s << "] ";

  // Meta (pid/tid)
  if (cfg_.include_pid || cfg_.include_tid) {
    os << "(";
    bool first = true;
    if (cfg_.include_pid) {
#if defined(_WIN32)
      int pid = _getpid();
#else
      int pid = static_cast<int>(::getpid());
#endif
      os << "pid=" << pid;
      first = false;
    }
    if (cfg_.include_tid) {
      if (!first)
        os << " ";
      std::ostringstream tid;
      tid << std::this_thread::get_id();
      os << "tid=" << tid.str();
    }
    os << ") ";
  }

  // Source (optional)
  if (cfg_.src_style != source_style::none) {
    std::string file_part =
        (cfg_.src_style == source_style::basename ? basename_of(file)
                                                  : (file ? file : ""));
    if (!file_part.empty()) {
      os << "(" << file_part << ":" << line;
      if (func && *func)
        os << " " << func;
      os << ") ";
    }
  }
  return os.str();
}

// ---------- submission ----------

void logger::submit(level lvl, const std::string &msg, const char *file,
                    int line, const char *func, const char *tag) {
  std::lock_guard<std::mutex> lock(m_);
  if (lvl < level_.load(std::memory_order_relaxed))
    return;

  roll_to_today_unlocked();

  // Optimize: only format the message once since console and file use same format
  // (in the future, for_file_sink parameter could be used to differentiate)
  const bool need_console = cfg_.console_enabled && !tui_mode_;
  const bool need_file = !current_path_.empty();

  if (!need_console && !need_file)
    return; // Early exit if no output needed

  const std::string formatted_line =
      format_prefix(lvl, file, line, func, tag, false) + msg + "\n";

  if (need_console) {
    if (cfg_.console_colors)
      std::cerr << level_color(lvl) << formatted_line << "\033[0m";
    else
      std::cerr << formatted_line;
  }

  if (need_file) {
    open_file_if_needed();
    if (fout_.is_open()) {
      if (cfg_.color_in_file && cfg_.console_colors) {
        fout_ << level_color(lvl) << formatted_line << "\033[0m";
      } else {
        fout_ << formatted_line;
      }
      if (tui_mode_ || lvl >= level::ERROR)
        fout_.flush();
      maybe_rotate();
    }
  }

  if (lvl == level::FATAL) {
    if (!tui_mode_)
      std::cerr << "\033[91mFATAL encountered. Aborting.\033[0m\n";
    std::terminate();
  }
}

// ---------- convenience ----------

void logger::debug(const std::string &tag, const std::string &msg) {
  submit(level::DEBUG, msg, __FILE__, __LINE__, __func__, tag.c_str());
}
void logger::info(const std::string &tag, const std::string &msg) {
  submit(level::INFO, msg, __FILE__, __LINE__, __func__, tag.c_str());
}
void logger::succ(const std::string &tag, const std::string &msg) {
  submit(level::SUCCESS, msg, __FILE__, __LINE__, __func__, tag.c_str());
}
void logger::warn(const std::string &tag, const std::string &msg) {
  submit(level::WARN, msg, __FILE__, __LINE__, __func__, tag.c_str());
}
void logger::error(const std::string &tag, const std::string &msg) {
  submit(level::ERROR, msg, __FILE__, __LINE__, __func__, tag.c_str());
}

} // namespace nazg::blackbox

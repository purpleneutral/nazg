#pragma once
#include <string>

namespace nazg::blackbox {

enum class level { TRACE = 0, DEBUG, INFO, SUCCESS, WARN, ERROR, FATAL, OFF };

// Parse level from string (case-insensitive)
// Returns default_level if string doesn't match any level
level parse_level(const std::string &str, level default_level = level::INFO);

} // namespace nazg::blackbox

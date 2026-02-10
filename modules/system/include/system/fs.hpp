#pragma once
#include <string>
#include <vector>

namespace nazg::system {

// Trim leading and trailing whitespace
std::string trim(const std::string &s);

// Read first line from a file
std::string read_file_line(const std::string &path);

// Format bytes in human-readable form (GiB)
std::string bytes_gib(unsigned long long bytes);

// Expand ~ to HOME directory
std::string expand_tilde(const std::string &path);

// Text wrapping for terminal output
std::vector<std::string> wrap_text(const std::string &s, int width);

} // namespace nazg::system

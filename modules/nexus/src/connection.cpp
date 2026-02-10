#include "nexus/connection.hpp"
#include <algorithm>

namespace nazg::nexus {

std::optional<std::string>
QueryResult::Row::get(const std::string &col) const {
  auto it = std::find(columns.begin(), columns.end(), col);
  if (it == columns.end())
    return std::nullopt;
  size_t idx = std::distance(columns.begin(), it);
  if (idx >= values.size())
    return std::nullopt;
  return values[idx];
}

std::optional<int64_t>
QueryResult::Row::get_int(const std::string &col) const {
  auto val = get(col);
  if (!val || val->empty())
    return std::nullopt;
  try {
    return std::stoll(*val);
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace nazg::nexus

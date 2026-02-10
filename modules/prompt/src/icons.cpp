#include "prompt/icons.hpp"

namespace nazg::prompt {

IconSet::IconSet(bool use_unicode) : unicode_(use_unicode) {}

std::string IconSet::get(Icon icon) const {
  if (unicode_) {
    switch (icon) {
      case Icon::SUCCESS:           return "✓";
      case Icon::ERROR:             return "✗";
      case Icon::WARNING:           return "⚠";
      case Icon::INFO:              return "ℹ";
      case Icon::PROGRESS:          return "⏳";
      case Icon::ARROW:             return "→";
      case Icon::BULLET:            return "•";
      case Icon::CHECKBOX:          return "☐";
      case Icon::CHECKBOX_CHECKED:  return "☑";
    }
  } else {
    switch (icon) {
      case Icon::SUCCESS:           return "[OK]";
      case Icon::ERROR:             return "[FAIL]";
      case Icon::WARNING:           return "[WARN]";
      case Icon::INFO:              return "[INFO]";
      case Icon::PROGRESS:          return "[...]";
      case Icon::ARROW:             return ">";
      case Icon::BULLET:            return "*";
      case Icon::CHECKBOX:          return "[ ]";
      case Icon::CHECKBOX_CHECKED:  return "[x]";
    }
  }
  return "";
}

} // namespace nazg::prompt

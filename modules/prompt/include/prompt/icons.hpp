#pragma once
#include <string>

namespace nazg::prompt {

enum class Icon {
  SUCCESS,
  ERROR,
  WARNING,
  INFO,
  PROGRESS,
  ARROW,
  BULLET,
  CHECKBOX,
  CHECKBOX_CHECKED
};

class IconSet {
public:
  explicit IconSet(bool use_unicode = true);

  std::string get(Icon icon) const;
  std::string success() const { return get(Icon::SUCCESS); }
  std::string error() const { return get(Icon::ERROR); }
  std::string warning() const { return get(Icon::WARNING); }
  std::string info() const { return get(Icon::INFO); }
  std::string progress() const { return get(Icon::PROGRESS); }
  std::string arrow() const { return get(Icon::ARROW); }
  std::string bullet() const { return get(Icon::BULLET); }

private:
  bool unicode_;
};

} // namespace nazg::prompt

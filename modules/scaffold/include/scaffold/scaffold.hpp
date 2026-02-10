#pragma once
#include <optional>
#include <string>

namespace nazg::blackbox {
class logger;
}

namespace nazg::scaffold {

enum class Language {
  C,
  CPP,
  PYTHON
};

struct ScaffoldSpec {
  Language lang;
  std::string name;
  std::string root;        // e.g., /home/user/projects or "." for current dir
  bool create_venv = true; // python
  bool use_direnv = true;  // python .envrc
  bool in_place = false;   // if true, scaffold directly in current dir (don't create subdirectory)
};

struct ScaffoldResult {
  bool created = false;
  std::string path; // project root
  std::string message;
};

ScaffoldResult scaffold_project(const ScaffoldSpec &spec,
                                nazg::blackbox::logger *log = nullptr);

} // namespace nazg::scaffold

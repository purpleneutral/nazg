#pragma once
#include <string>

namespace nazg::blackbox {
class logger;
}

namespace nazg::git {

enum class Language { C, CPP, PYTHON, RUST, GO, JAVASCRIPT, UNKNOWN };

// Git maintenance operations
class Maintenance {
public:
  explicit Maintenance(nazg::blackbox::logger *log = nullptr);

  // Generate .gitignore for language
  bool generate_gitignore(Language lang, const std::string &repo_path);

  // Ensure initial commit exists
  bool ensure_initial_commit(const std::string &repo_path,
                             const std::string &message = "Initial commit");

  // Generate .gitattributes
  bool generate_gitattributes(Language lang, const std::string &repo_path);

private:
  nazg::blackbox::logger *log_;

  std::string get_gitignore_template(Language lang) const;
  std::string get_gitattributes_template(Language lang) const;
};

} // namespace nazg::git

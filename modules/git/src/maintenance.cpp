#include "git/maintenance.hpp"
#include "blackbox/logger.hpp"
#include <filesystem>
#include <fstream>
#include <array>
#include <cstdio>
#include <memory>

namespace fs = std::filesystem;

namespace nazg::git {

namespace {
int exec_cmd(const std::string &cmd) { return std::system(cmd.c_str()); }

bool file_exists(const std::string &path) {
  return fs::exists(path) && fs::is_regular_file(path);
}
} // namespace

Maintenance::Maintenance(nazg::blackbox::logger *log) : log_(log) {}

bool Maintenance::generate_gitignore(Language lang,
                                     const std::string &repo_path) {
  std::string gitignore_path = repo_path + "/.gitignore";

  // Don't overwrite existing .gitignore
  if (file_exists(gitignore_path)) {
    if (log_) {
      log_->info("Git", ".gitignore already exists");
    }
    return true;
  }

  std::string content = get_gitignore_template(lang);
  std::ofstream out(gitignore_path);
  if (!out) {
    if (log_) {
      log_->error("Git", "Failed to create .gitignore");
    }
    return false;
  }

  out << content;
  if (log_) {
    log_->info("Git", "Generated .gitignore");
  }
  return true;
}

bool Maintenance::ensure_initial_commit(const std::string &repo_path,
                                        const std::string &message) {
  // Check if commits exist
  std::string check_cmd = "cd \"" + repo_path + "\" && git rev-parse HEAD >/dev/null 2>&1";
  if (exec_cmd(check_cmd) == 0) {
    if (log_) {
      log_->info("Git", "Repository already has commits");
    }
    return true;
  }

  if (log_) {
    log_->info("Git", "Creating initial commit");
  }

  // Add all files and commit
  std::string cmd = "cd \"" + repo_path + "\" && git add -A && git commit -m \"" + message + "\"";
  return exec_cmd(cmd) == 0;
}

bool Maintenance::generate_gitattributes(Language lang,
                                         const std::string &repo_path) {
  std::string path = repo_path + "/.gitattributes";
  if (file_exists(path)) {
    return true;
  }

  std::string content = get_gitattributes_template(lang);
  if (content.empty()) {
    return true; // No template needed
  }

  std::ofstream out(path);
  if (!out) {
    return false;
  }

  out << content;
  if (log_) {
    log_->info("Git", "Generated .gitattributes");
  }
  return true;
}

std::string Maintenance::get_gitignore_template(Language lang) const {
  std::string common = R"(# Common
.DS_Store
*.swp
*.swo
*~
.direnv/
.envrc

)";

  switch (lang) {
  case Language::C:
  case Language::CPP:
    return common + R"(# C/C++
*.o
*.a
*.so
*.dylib
*.exe
*.out
build/
.cache/
compile_commands.json
)";

  case Language::PYTHON:
    return common + R"(# Python
__pycache__/
*.py[cod]
*$py.class
*.so
.Python
.venv/
venv/
ENV/
env/
*.egg-info/
dist/
build/
.pytest_cache/
.coverage
htmlcov/
)";

  case Language::RUST:
    return common + R"(# Rust
target/
Cargo.lock
**/*.rs.bk
)";

  case Language::GO:
    return common + R"(# Go
*.exe
*.exe~
*.dll
*.so
*.dylib
*.test
*.out
vendor/
)";

  case Language::JAVASCRIPT:
    return common + R"(# JavaScript/Node
node_modules/
npm-debug.log*
yarn-debug.log*
yarn-error.log*
.npm
dist/
build/
.next/
)";

  default:
    return common;
  }
}

std::string Maintenance::get_gitattributes_template(Language lang) const {
  switch (lang) {
  case Language::C:
  case Language::CPP:
    return R"(# C/C++
*.c text
*.cpp text
*.h text
*.hpp text
)";

  case Language::PYTHON:
    return R"(# Python
*.py text
*.pyx text
*.pyd binary
)";

  default:
    return "";
  }
}

} // namespace nazg::git

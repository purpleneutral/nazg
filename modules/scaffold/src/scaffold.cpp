#include "scaffold/scaffold.hpp"
#include "scaffold/templates.hpp"
#include "blackbox/logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace nazg::scaffold {

static void write_file(const fs::path &path, const std::string &content, nazg::blackbox::logger *log) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path);
  if (!file) {
    if (log) {
      log->error("Scaffold", "Failed to write file: " + path.string());
    }
    throw std::runtime_error("Failed to write: " + path.string());
  }
  file << content;
  if (log) {
    log->debug("Scaffold", "Created file: " + path.string());
  }
}

static bool run_command(const std::string &cmd, nazg::blackbox::logger *log) {
  if (log) {
    log->debug("Scaffold", "Running command: " + cmd);
  }
  int ret = std::system(cmd.c_str());
  if (ret != 0 && log) {
    log->warn("Scaffold", "Command failed with exit code " + std::to_string(ret));
  }
  return ret == 0;
}

ScaffoldResult scaffold_project(const ScaffoldSpec &spec,
                                nazg::blackbox::logger *log) {
  ScaffoldResult result;

  // Resolve root path
  fs::path root_path = spec.root;
  if (root_path.empty() || root_path == ".") {
    root_path = fs::current_path();
  }

  fs::path project_path;

  if (spec.in_place) {
    // Initialize in current directory
    project_path = root_path;

    // Check if directory is empty (allow hidden files like .git)
    bool has_files = false;
    for (const auto &entry : fs::directory_iterator(project_path)) {
      std::string filename = entry.path().filename().string();
      // Ignore hidden files
      if (filename[0] != '.') {
        has_files = true;
        break;
      }
    }

    if (has_files) {
      result.message = "Directory is not empty: " + project_path.string();
      if (log) {
        log->warn("Scaffold", result.message + " (proceeding anyway)");
      }
    }
  } else {
    // Create subdirectory
    project_path = root_path / spec.name;

    // Check if directory already exists
    if (fs::exists(project_path)) {
      result.message = "Directory already exists: " + project_path.string();
      if (log) {
        log->error("Scaffold", result.message);
      }
      return result;
    }
  }

  try {
    // Create project directory structure
    fs::create_directories(project_path / "src");

    if (log) {
      log->info("Scaffold", "Creating project: " + project_path.string());
    }

    // Generate files based on language
    switch (spec.lang) {
      case Language::C: {
        write_file(project_path / "CMakeLists.txt",
                  templates::cmake_c(spec.name), log);
        write_file(project_path / "src/main.c",
                  templates::main_c(), log);
        write_file(project_path / ".gitignore",
                  templates::cpp_gitignore(), log);
        write_file(project_path / "README.md",
                  templates::generic_readme(spec.name, "C"), log);

        if (log) {
          log->info("Scaffold", "Created C project with CMakeLists.txt");
        }
        break;
      }

      case Language::CPP: {
        write_file(project_path / "CMakeLists.txt",
                  templates::cmake_cpp(spec.name), log);
        write_file(project_path / "src/main.cpp",
                  templates::main_cpp(), log);
        write_file(project_path / ".gitignore",
                  templates::cpp_gitignore(), log);
        write_file(project_path / "README.md",
                  templates::generic_readme(spec.name, "C++"), log);

        if (log) {
          log->info("Scaffold", "Created C++ project with CMakeLists.txt");
        }
        break;
      }

      case Language::PYTHON: {
        write_file(project_path / "src/main.py",
                  templates::python_main(spec.name), log);
        write_file(project_path / "requirements.txt",
                  templates::python_requirements(), log);
        write_file(project_path / ".gitignore",
                  templates::python_gitignore(), log);
        write_file(project_path / "README.md",
                  templates::python_readme(spec.name), log);

        // Create .envrc for direnv if requested
        if (spec.use_direnv) {
          write_file(project_path / ".envrc",
                    templates::python_envrc(), log);
          if (log) {
            log->info("Scaffold", "Created .envrc for direnv");
          }
        }

        // Create virtual environment if requested
        if (spec.create_venv) {
          std::string venv_path = (project_path / ".venv").string();
          std::string cmd = "python3 -m venv \"" + venv_path + "\"";

          if (log) {
            log->info("Scaffold", "Creating virtual environment...");
          }

          if (run_command(cmd, log)) {
            if (log) {
              log->info("Scaffold", "Virtual environment created: .venv/");
            }
          } else {
            if (log) {
              log->warn("Scaffold", "Failed to create virtual environment");
            }
          }
        }

        if (log) {
          log->info("Scaffold", "Created Python project");
        }
        break;
      }
    }

    result.created = true;
    result.path = project_path.string();
    result.message = "Project created successfully";

    if (log) {
      log->info("Scaffold", "✓ Project scaffolded: " + result.path);
    }

  } catch (const std::exception &e) {
    result.message = std::string("Failed to create project: ") + e.what();
    if (log) {
      log->error("Scaffold", result.message);
    }
  }

  return result;
}

} // namespace nazg::scaffold

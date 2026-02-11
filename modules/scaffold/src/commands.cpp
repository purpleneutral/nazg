// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 purpleneutral
//
// This file is part of nazg.
//
// nazg is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// nazg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with nazg. If not, see <https://www.gnu.org/licenses/>.

#include "scaffold/commands.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include "scaffold/scaffold.hpp"
#include "blackbox/logger.hpp"
#include "prompt/prompt.hpp"
#include "git/client.hpp"
#include "git/bare.hpp"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unistd.h>

namespace nazg::scaffold {

static std::string to_lower(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return str;
}

static scaffold::Language parse_language(const std::string &lang_str) {
  std::string lower = to_lower(lang_str);
  if (lower == "c") {
    return scaffold::Language::C;
  } else if (lower == "cpp" || lower == "c++" || lower == "cxx") {
    return scaffold::Language::CPP;
  } else if (lower == "python" || lower == "py") {
    return scaffold::Language::PYTHON;
  }
  throw std::runtime_error("Unknown language: " + lang_str);
}

// nazg init <language> [project-name] [options]
static int cmd_init(const directive::command_context &ctx,
                    const directive::context &ectx) {

  // Parse arguments from ctx (command context)
  // argv[0] = program name
  // argv[1] = "init"
  // argv[2] = language
  // argv[3] = project-name (optional - uses current dir name if omitted)
  // argv[4+] = options

  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg init <language> [project-name] [options]\n";
    std::cerr << "\nLanguages:\n";
    std::cerr << "  c, cpp, python\n";
    std::cerr << "\nProject Options:\n";
    std::cerr << "  --no-venv        Don't create Python virtual environment\n";
    std::cerr << "  --no-direnv      Don't create .envrc for direnv\n";
    std::cerr << "\nGit Options:\n";
    std::cerr << "  --no-git         Don't initialize git repository\n";
    std::cerr << "  --with-bare      Create bare repository and link as origin\n";
    std::cerr << "\nPrompt Options:\n";
    std::cerr << "  -y, --yes        Skip confirmation prompt\n";
    std::cerr << "  -n, --no         Answer no to confirmation (cancel)\n";
    std::cerr << "  -v, --verbose    Verbose prompt output\n";
    std::cerr << "  -m, --minimal    Minimal prompt output\n";
    std::cerr << "\nExamples:\n";
    std::cerr << "  nazg init cpp my-project              # Create subdirectory with git\n";
    std::cerr << "  nazg init python                      # Initialize in current directory\n";
    std::cerr << "  nazg init c hello-world --yes         # Skip confirmation\n";
    std::cerr << "  nazg init cpp my-app --with-bare      # Create with bare repo\n";
    std::cerr << "  nazg init python my-api --no-git      # Without git initialization\n";
    return 1;
  }

  std::string lang_str = ctx.argv[2];

  if (ectx.log) {
    ectx.log->debug("Scaffold::init", "Language argument: " + lang_str);
  }

  // Determine project name and mode
  std::string project_name;
  bool in_place = false;
  int flags_start_index = 4;

  if (ctx.argc >= 4 && ctx.argv[3][0] != '-') {
    // Project name provided
    project_name = ctx.argv[3];
    flags_start_index = 4;
    if (ectx.log) {
      ectx.log->debug("Scaffold::init", "Project name: " + project_name + " (create subdirectory)");
    }
  } else {
    // No project name - use current directory name and init in place
    char cwd[4096];
    if (::getcwd(cwd, sizeof(cwd))) {
      std::filesystem::path cwd_path(cwd);
      project_name = cwd_path.filename().string();
      in_place = true;
      flags_start_index = 3;
      if (ectx.log) {
        ectx.log->debug("Scaffold::init", "Project name: " + project_name + " (in-place mode)");
      }
    } else {
      if (ectx.log) {
        ectx.log->error("Scaffold::init", "Could not determine current directory");
      }
      std::cerr << "Error: Could not determine current directory\n";
      return 1;
    }
  }

  // Parse flags
  bool create_venv = true;
  bool use_direnv = true;
  bool init_git = true;
  bool create_bare = false;
  bool force_yes = false;
  bool force_no = false;
  prompt::Style prompt_style = prompt::Style::STANDARD;

  // Check global verbose flag from context
  if (ectx.verbose) {
    prompt_style = prompt::Style::VERBOSE;
  }

  for (int i = flags_start_index; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--no-venv") {
      create_venv = false;
    } else if (arg == "--no-direnv") {
      use_direnv = false;
    } else if (arg == "--no-git") {
      init_git = false;
    } else if (arg == "--with-bare") {
      create_bare = true;
      init_git = true;  // Implies git
    } else if (arg == "--yes" || arg == "-y") {
      force_yes = true;
    } else if (arg == "--no" || arg == "-n") {
      force_no = true;
    } else if (arg == "--verbose" || arg == "-v") {
      prompt_style = prompt::Style::VERBOSE;
    } else if (arg == "--minimal" || arg == "-m") {
      prompt_style = prompt::Style::MINIMAL;
    }
  }

  // Log parsed flags
  if (ectx.log) {
    ectx.log->debug("Scaffold::init", "Flags: venv=" + std::string(create_venv ? "yes" : "no") +
                                     ", direnv=" + std::string(use_direnv ? "yes" : "no") +
                                     ", git=" + std::string(init_git ? "yes" : "no") +
                                     ", bare=" + std::string(create_bare ? "yes" : "no") +
                                     ", force_yes=" + std::string(force_yes ? "yes" : "no") +
                                     ", force_no=" + std::string(force_no ? "yes" : "no"));

    std::string style_str = (prompt_style == prompt::Style::MINIMAL) ? "minimal" :
                           (prompt_style == prompt::Style::VERBOSE) ? "verbose" : "standard";
    ectx.log->debug("Scaffold::init", "Prompt style: " + style_str);
  }

  // Parse language
  scaffold::Language lang;
  try {
    lang = parse_language(lang_str);
    if (ectx.log) {
      ectx.log->debug("Scaffold::init", "Parsed language: " + lang_str);
    }
  } catch (const std::exception &e) {
    if (ectx.log) {
      ectx.log->error("Scaffold::init", std::string("Invalid language: ") + e.what());
    }
    std::cerr << "Error: " << e.what() << "\n";
    std::cerr << "Supported languages: c, cpp, python\n";
    return 1;
  }

  // Build scaffold spec
  scaffold::ScaffoldSpec spec;
  spec.lang = lang;
  spec.name = project_name;
  spec.root = in_place ? "." : ".";  // Always use current dir as root
  spec.create_venv = create_venv;
  spec.use_direnv = use_direnv;
  spec.in_place = in_place;

  // ── Prompt for confirmation ──
  prompt::Prompt confirm_prompt(ectx.log);
  confirm_prompt.title("init")
                .style(prompt_style)
                .force_yes(force_yes)
                .force_no(force_no);

  // Show project context
  std::string target_path;
  if (in_place) {
    char cwd[4096];
    if (::getcwd(cwd, sizeof(cwd))) {
      target_path = cwd;
    }
    confirm_prompt.question("Initialize " + lang_str + " project in current directory?");
  } else {
    char cwd[4096];
    std::string base_path = ".";
    if (::getcwd(cwd, sizeof(cwd))) {
      base_path = cwd;
    }
    target_path = std::string(base_path) + "/" + project_name;
    confirm_prompt.question("Create new " + lang_str + " project?");
  }

  // Add facts about what will be created
  confirm_prompt.fact("Project name", project_name)
                .fact("Language", lang_str)
                .fact("Target path", target_path);

  // Language-specific facts
  if (lang == scaffold::Language::PYTHON) {
    confirm_prompt.fact("Virtual env", create_venv ? "yes (.venv/)" : "no");
    if (use_direnv) {
      confirm_prompt.fact("Direnv", "yes (.envrc)");
    }
  }

  // Show what will be created
  confirm_prompt.action("Create project directory structure");

  if (lang == scaffold::Language::C) {
    confirm_prompt.action("Generate CMakeLists.txt for C");
    confirm_prompt.action("Generate src/main.c");
  } else if (lang == scaffold::Language::CPP) {
    confirm_prompt.action("Generate CMakeLists.txt for C++");
    confirm_prompt.action("Generate src/main.cpp");
  } else if (lang == scaffold::Language::PYTHON) {
    confirm_prompt.action("Generate src/main.py");
    confirm_prompt.action("Generate requirements.txt");
    if (create_venv) {
      confirm_prompt.action("Create Python virtual environment");
    }
    if (use_direnv) {
      confirm_prompt.action("Create .envrc for direnv");
    }
  }

  confirm_prompt.action("Generate .gitignore")
                .action("Generate README.md");

  // Git actions
  if (init_git) {
    confirm_prompt.action("Initialize git repository (main branch)");
    confirm_prompt.action("Create initial commit");

    if (create_bare) {
      std::string username = std::getenv("USER") ? std::getenv("USER") : "user";
      std::string bare_path = "/home/" + username + "/repos/" + project_name + ".git";
      confirm_prompt.action("Create bare repository at " + bare_path);
      confirm_prompt.action("Link to bare as 'origin' remote");
      confirm_prompt.action("Push initial commit to origin");
    }
  }

  // Warnings
  if (in_place) {
    confirm_prompt.warning("Scaffolding in current directory (no subdirectory will be created)");
  }

  // Ask for confirmation
  if (ectx.log) {
    ectx.log->debug("Scaffold::init", "Displaying confirmation prompt to user");
  }

  bool confirmed = confirm_prompt.confirm(true);

  // Log user's decision
  if (ectx.log) {
    ectx.log->info("Scaffold::init", "User response: " + std::string(confirmed ? "confirmed" : "cancelled"));
  }

  if (!confirmed) {
    if (ectx.log) {
      ectx.log->info("Scaffold::init", "Project creation cancelled by user");
    }
    std::cout << "Cancelled.\n";
    return 0;
  }

  // Log the confirmed action
  if (ectx.log) {
    if (in_place) {
      ectx.log->info("Scaffold::init", "User confirmed: Initializing " + lang_str + " project in place: " + project_name);
    } else {
      ectx.log->info("Scaffold::init", "User confirmed: Creating " + lang_str + " project: " + project_name);
    }
    ectx.log->info("Scaffold::init", "Target path: " + target_path);
  }

  auto result = scaffold::scaffold_project(spec, ectx.log);

  if (result.created) {
    if (ectx.log) {
      ectx.log->info("Scaffold", "Successfully created project at: " + result.path);
    }

    // ── Git initialization ──
    bool git_success = true;
    std::string bare_path_created;

    if (init_git) {
      if (ectx.log) {
        ectx.log->info("Scaffold::git", "Initializing git repository at: " + result.path);
      }

      git::Client client(result.path, ectx.log);

      // Initialize repository
      if (!client.init("main")) {
        if (ectx.log) {
          ectx.log->error("Scaffold::git", "Failed to initialize git repository");
        }
        std::cerr << "Warning: Failed to initialize git repository\n";
        git_success = false;
      } else {
        if (ectx.log) {
          ectx.log->info("Scaffold::git", "Git repository initialized");
        }

        // Set identity
        auto cfg = client.get_config();
        if (!cfg.user_name.has_value() || !cfg.user_email.has_value()) {
          std::string username = std::getenv("USER") ? std::getenv("USER") : "user";
          std::string email = username + "@localhost";
          client.ensure_identity(username, email);
          if (ectx.log) {
            ectx.log->info("Scaffold::git", "Set git identity: " + username + " <" + email + ">");
          }
        }

        // Initial commit
        client.add_all();
        if (client.commit("Initial commit")) {
          if (ectx.log) {
            ectx.log->info("Scaffold::git", "Created initial commit");
          }
        } else {
          if (ectx.log) {
            ectx.log->error("Scaffold::git", "Failed to create initial commit");
          }
        }

        // Create bare repo if requested
        if (create_bare && git_success) {
          git::BareManager bare_mgr(ectx.store, ectx.log);
          bare_path_created = bare_mgr.default_bare_path(project_name);

          if (ectx.log) {
            ectx.log->info("Scaffold::git", "Creating bare repository at: " + bare_path_created);
          }

          if (!bare_mgr.create_bare(bare_path_created)) {
            if (ectx.log) {
              ectx.log->error("Scaffold::git", "Failed to create bare repository");
            }
            std::cerr << "Warning: Failed to create bare repository\n";
          } else {
            if (ectx.log) {
              ectx.log->info("Scaffold::git", "Bare repository created");
            }

            // Link to bare
            if (!bare_mgr.link_to_bare(result.path, bare_path_created)) {
              if (ectx.log) {
                ectx.log->error("Scaffold::git", "Failed to link to bare repository");
              }
              std::cerr << "Warning: Failed to link to bare repository\n";
            } else {
              if (ectx.log) {
                ectx.log->info("Scaffold::git", "Linked to bare repository as origin");
              }

              // Push to bare
              if (client.push("origin", "main")) {
                if (ectx.log) {
                  ectx.log->info("Scaffold::git", "Pushed to bare repository");
                }
              } else {
                if (ectx.log) {
                  ectx.log->error("Scaffold::git", "Failed to push to bare repository");
                }
                std::cerr << "Warning: Failed to push to bare repository\n";
              }
            }
          }
        }
      }
    }

    // ── Success prompt with next steps ──
    prompt::Prompt success_prompt(ectx.log);
    success_prompt.title("init")
                  .style(prompt_style)
                  .question("✓ Project created successfully!");

    success_prompt.fact("Location", result.path);

    if (init_git && git_success) {
      success_prompt.fact("Git", "initialized (main)");
      if (!bare_path_created.empty()) {
        success_prompt.fact("Bare repo", bare_path_created);
      }
    }

    success_prompt.info("Ready to start coding!");

    // Show next steps as details
    if (!in_place) {
      success_prompt.detail("Next: cd " + project_name);
    }

    if (lang == scaffold::Language::PYTHON) {
      if (use_direnv) {
        success_prompt.detail("Next: direnv allow")
                      .detail("Then: pip install -r requirements.txt")
                      .detail("Run: python src/main.py");
      } else if (create_venv) {
        success_prompt.detail("Next: source .venv/bin/activate")
                      .detail("Then: pip install -r requirements.txt")
                      .detail("Run: python src/main.py");
      } else {
        success_prompt.detail("Next: pip install -r requirements.txt")
                      .detail("Run: python src/main.py");
      }
    } else {
      success_prompt.detail("Next: nazg build")
                    .detail("Run: ./build/" + project_name);
    }

    if (prompt_style == prompt::Style::MINIMAL) {
      std::cout << "\n✓ Project created: " << result.path << "\n";
      if (init_git && git_success) {
        std::cout << "  Git: initialized (main)";
        if (!bare_path_created.empty()) {
          std::cout << " + bare repo";
        }
        std::cout << "\n";
      }
      if (!in_place) {
        std::cout << "  cd " << project_name << "\n";
      }
      if (lang == scaffold::Language::PYTHON) {
        if (use_direnv) {
          std::cout << "  direnv allow && pip install -r requirements.txt\n";
        } else if (create_venv) {
          std::cout << "  source .venv/bin/activate && pip install -r requirements.txt\n";
        }
        std::cout << "  python src/main.py\n";
      } else {
        std::cout << "  nazg build && ./build/" << project_name << "\n";
      }
    } else {
      // For standard/verbose, use a nice box
      std::cout << "\n";
      std::cout << "┌────────────────────────────────────────────────────────────\n";
      std::cout << "│ ✓ Project created successfully!\n";
      std::cout << "│\n";
      std::cout << "│ Location: " << result.path << "\n";
      if (init_git && git_success) {
        std::cout << "│ Git: initialized (main)\n";
        if (!bare_path_created.empty()) {
          std::cout << "│ Bare repo: " << bare_path_created << "\n";
        }
      }
      std::cout << "├────────────────────────────────────────────────────────────\n";
      std::cout << "│ Next steps:\n";
      if (!in_place) {
        std::cout << "│   cd " << project_name << "\n";
      }
      if (lang == scaffold::Language::PYTHON) {
        if (use_direnv) {
          std::cout << "│   direnv allow\n";
          std::cout << "│   pip install -r requirements.txt\n";
        } else if (create_venv) {
          std::cout << "│   source .venv/bin/activate\n";
          std::cout << "│   pip install -r requirements.txt\n";
        }
        std::cout << "│   python src/main.py\n";
      } else {
        std::cout << "│   nazg build\n";
        std::cout << "│   ./build/" << project_name << "\n";
      }
      std::cout << "└────────────────────────────────────────────────────────────\n";
    }

    return 0;
  } else {
    if (ectx.log) {
      ectx.log->error("Scaffold", "Failed to create project: " + result.message);
    }

    // ── Error prompt ──
    prompt::Prompt error_prompt(ectx.log);
    error_prompt.title("init")
                .style(prompt_style)
                .question("✗ Project creation failed");

    error_prompt.warning(result.message);

    // Render error message
    if (prompt_style == prompt::Style::MINIMAL) {
      std::cerr << "✗ Failed: " << result.message << "\n";
    } else {
      std::cerr << "\n";
      std::cerr << "┌────────────────────────────────────────────────────────────\n";
      std::cerr << "│ ✗ Project creation failed\n";
      std::cerr << "│\n";
      std::cerr << "│ Error: " << result.message << "\n";
      std::cerr << "└────────────────────────────────────────────────────────────\n";
    }

    return 1;
  }
}

void register_commands(directive::registry &reg, const directive::context &/*ctx*/) {
  directive::command_spec spec;
  spec.name = "init";
  spec.summary = "Create a new project (c, cpp, python)";
  spec.run = cmd_init;
  reg.add(spec);
}

} // namespace nazg::scaffold

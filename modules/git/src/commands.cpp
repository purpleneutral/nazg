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

#include "git/commands.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include "git/bare.hpp"
#include "git/client.hpp"
#include "git/maintenance.hpp"
#include "git/server.hpp"
#include "git/cgit.hpp"
#include "git/gitea.hpp"
#include "git/server_registry.hpp"
#include "git/migration.hpp"
#include "nexus/store.hpp"
#include "blackbox/logger.hpp"
#include "prompt/prompt.hpp"
#include "prompt/colors.hpp"
#include "system/process.hpp"

#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <ctime>
#include <cstdlib>

namespace fs = std::filesystem;

namespace nazg::git {

namespace {
std::string get_cwd() {
  char buf[4096];
  if (::getcwd(buf, sizeof(buf))) {
    return std::string(buf);
  }
  return ".";
}

// Convert string to lowercase (for case-insensitive command matching)
static std::string to_lower_copy(std::string text) {
  for (char &c : text) {
    c = std::tolower(static_cast<unsigned char>(c));
  }
  return text;
}

// Parse common prompt flags
struct PromptFlags {
  bool force_yes = false;
  bool force_no = false;
  prompt::Style style = prompt::Style::STANDARD;
};

PromptFlags parse_prompt_flags(const directive::command_context &ctx,
                               const directive::context &ectx,
                               int start_index = 3) {
  PromptFlags flags;

  if (ectx.verbose) {
    flags.style = prompt::Style::VERBOSE;
  }

  for (int i = start_index; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--yes" || arg == "-y") {
      flags.force_yes = true;
    } else if (arg == "--no" || arg == "-n") {
      flags.force_no = true;
    } else if (arg == "--verbose" || arg == "-v") {
      flags.style = prompt::Style::VERBOSE;
    } else if (arg == "--minimal" || arg == "-m") {
      flags.style = prompt::Style::MINIMAL;
    }
  }

  return flags;
}
} // namespace

// nazg git init - Initialize git repository
static int cmd_git_init(const directive::command_context &ctx,
                        const directive::context &ectx) {
  std::string cwd = get_cwd();
  auto flags = parse_prompt_flags(ctx, ectx);

  git::Client client(cwd, ectx.log);

  if (client.is_repo()) {
    std::cout << "Already a git repository\n";
    return 0;
  }

  // Detect language for .gitignore
  git::Language lang = git::Language::UNKNOWN;
  std::string lang_name = "unknown";

  if (fs::exists("CMakeLists.txt") || fs::exists("Makefile")) {
    lang = git::Language::CPP;
    lang_name = "C++";
  } else if (fs::exists("setup.py") || fs::exists("pyproject.toml")) {
    lang = git::Language::PYTHON;
    lang_name = "Python";
  } else if (fs::exists("Cargo.toml")) {
    lang = git::Language::RUST;
    lang_name = "Rust";
  } else if (fs::exists("go.mod")) {
    lang = git::Language::GO;
    lang_name = "Go";
  } else if (fs::exists("package.json")) {
    lang = git::Language::JAVASCRIPT;
    lang_name = "JavaScript";
  }

  // ── Prompt for confirmation ──
  prompt::Prompt confirm_prompt(ectx.log);
  confirm_prompt.title("git-init")
                .style(flags.style)
                .force_yes(flags.force_yes)
                .force_no(flags.force_no)
                .question("Initialize git repository?");

  confirm_prompt.fact("Directory", cwd)
                .fact("Branch", "main")
                .fact("Language", lang_name);

  confirm_prompt.action("Initialize git repository (main branch)");

  if (lang != git::Language::UNKNOWN) {
    confirm_prompt.action("Generate .gitignore for " + lang_name);
  }

  // Check identity
  auto cfg = client.get_config();
  std::string username = std::getenv("USER") ? std::getenv("USER") : "user";
  std::string email = username + "@localhost";

  if (!cfg.user_name.has_value() || !cfg.user_email.has_value()) {
    confirm_prompt.action("Set git identity: " + username + " <" + email + ">");
  }

  confirm_prompt.action("Create initial commit");

  if (ectx.log) {
    ectx.log->debug("Git::init", "Displaying confirmation prompt");
  }

  bool confirmed = confirm_prompt.confirm(true);

  if (ectx.log) {
    ectx.log->info("Git::init", "User response: " + std::string(confirmed ? "confirmed" : "cancelled"));
  }

  if (!confirmed) {
    if (ectx.log) {
      ectx.log->info("Git::init", "Repository initialization cancelled by user");
    }
    std::cout << "Cancelled.\n";
    return 0;
  }

  // Initialize repository
  if (ectx.log) {
    ectx.log->info("Git::init", "Initializing git repository at: " + cwd);
  }

  if (!client.init("main")) {
    if (ectx.log) {
      ectx.log->error("Git::init", "Failed to initialize git repository");
    }
    std::cerr << "Failed to initialize git repository\n";
    return 1;
  }

  if (ectx.log) {
    ectx.log->info("Git::init", "Git repository initialized");
  }

  // Generate .gitignore
  git::Maintenance maint(ectx.log);
  if (lang != git::Language::UNKNOWN) {
    maint.generate_gitignore(lang, cwd);
    if (ectx.log) {
      ectx.log->info("Git::init", "Generated .gitignore for " + lang_name);
    }
  }

  // Ensure identity
  if (!cfg.user_name.has_value() || !cfg.user_email.has_value()) {
    client.ensure_identity(username, email);
    if (ectx.log) {
      ectx.log->info("Git::init", "Set git identity: " + username + " <" + email + ">");
    }
  }

  // Initial commit
  if (!client.has_commits()) {
    client.add_all();
    if (client.commit("Initial commit")) {
      if (ectx.log) {
        ectx.log->info("Git::init", "Created initial commit");
      }
    }
  }

  // ── Success message ──
  if (flags.style == prompt::Style::MINIMAL) {
    std::cout << "✓ Git repository initialized\n";
  } else {
    std::cout << "\n";
    std::cout << "┌────────────────────────────────────────────────────────────\n";
    std::cout << "│ ✓ Git repository initialized!\n";
    std::cout << "│\n";
    std::cout << "│ Branch: main\n";
    if (lang != git::Language::UNKNOWN) {
      std::cout << "│ .gitignore: " << lang_name << "\n";
    }
    std::cout << "│ Identity: " << username << " <" << email << ">\n";
    std::cout << "└────────────────────────────────────────────────────────────\n";
  }

  return 0;
}

// nazg git status - Show git status
static int cmd_git_status(const directive::command_context &/*ctx*/,
                          const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository\n";
    return 1;
  }

  auto status = client.status();

  // Create color formatter
  prompt::ColorFormatter fmt(true);

  // Helper to get state name
  auto get_state_name = [](git::RepoState state) -> std::string {
    switch (state) {
      case git::RepoState::MERGE: return "MERGE IN PROGRESS";
      case git::RepoState::REBASE: return "REBASE IN PROGRESS";
      case git::RepoState::REBASE_INTERACTIVE: return "INTERACTIVE REBASE";
      case git::RepoState::REBASE_MERGE: return "REBASE IN PROGRESS";
      case git::RepoState::CHERRY_PICK: return "CHERRY-PICK IN PROGRESS";
      case git::RepoState::REVERT: return "REVERT IN PROGRESS";
      case git::RepoState::BISECT: return "BISECT IN PROGRESS";
      default: return "";
    }
  };

  // Display branch with special state indicator
  std::cout << "Branch: " << fmt.cyan(status.branch);
  if (status.state != git::RepoState::NORMAL) {
    std::cout << " " << fmt.bold(fmt.red("(🔥 " + get_state_name(status.state) + ")"));
  }
  std::cout << "\n";

  if (status.upstream) {
    std::cout << "Upstream: " << fmt.dim(*status.upstream) << "\n";
  }
  if (status.ahead > 0 || status.behind > 0) {
    std::cout << "Divergence: " << fmt.yellow(std::to_string(status.ahead)) << " ahead, "
              << fmt.yellow(std::to_string(status.behind)) << " behind\n";
  }
  if (status.has_origin) {
    std::cout << "Origin: " << fmt.dim(status.origin_url.value()) << "\n";
  } else {
    std::cout << "Origin: " << fmt.dim("(not set)") << "\n";
  }

  // Show conflicts prominently if present
  if (status.conflicted > 0) {
    std::cout << "\n" << fmt.bold(fmt.red("⚠️  CONFLICTS DETECTED ⚠️")) << "\n";
    std::cout << fmt.red("Conflicted files: " + std::to_string(status.conflicted)) << "\n";
    for (const auto &file : status.conflicted_files) {
      std::cout << fmt.red("  ✗ " + file) << "\n";
    }
    std::cout << "\n";
  }

  std::cout << "\n" << fmt.bold("Status:") << "\n";
  if (status.conflicted > 0) {
    std::cout << "  " << fmt.red("Conflicted: " + std::to_string(status.conflicted)) << "\n";
  }
  std::cout << "  " << fmt.yellow("Modified:   " + std::to_string(status.modified)) << "\n";
  std::cout << "  " << fmt.green("Staged:     " + std::to_string(status.staged)) << "\n";
  std::cout << "  " << fmt.gray("Untracked:  " + std::to_string(status.untracked)) << "\n";

  // Show help for resolving conflicts
  if (status.conflicted > 0) {
    std::cout << "\n" << fmt.dim("To resolve conflicts:") << "\n";
    std::cout << fmt.dim("  1. Edit conflicted files to resolve conflicts") << "\n";
    std::cout << fmt.dim("  2. Stage resolved files: nazg git add <file>") << "\n";
    if (status.state == git::RepoState::REBASE || status.state == git::RepoState::REBASE_MERGE) {
      std::cout << fmt.dim("  3. Continue rebase: git rebase --continue") << "\n";
      std::cout << fmt.dim("  Or abort: git rebase --abort") << "\n";
    } else if (status.state == git::RepoState::MERGE) {
      std::cout << fmt.dim("  3. Complete merge: nazg git commit") << "\n";
      std::cout << fmt.dim("  Or abort: git merge --abort") << "\n";
    } else if (status.state == git::RepoState::CHERRY_PICK) {
      std::cout << fmt.dim("  3. Continue cherry-pick: git cherry-pick --continue") << "\n";
      std::cout << fmt.dim("  Or abort: git cherry-pick --abort") << "\n";
    }
  }

  // List remotes
  auto remotes = client.list_remotes();
  if (!remotes.empty()) {
    std::cout << "\n" << fmt.bold("Remotes:") << "\n";
    for (const auto &[name, url] : remotes) {
      std::cout << "  " << fmt.cyan(name) << " -> " << fmt.dim(url) << "\n";
    }
  }

  return 0;
}

// nazg git create-bare [path] - Create bare repository
static int cmd_git_create_bare(const directive::command_context &ctx,
                                const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository. Run 'nazg git init' first.\n";
    return 1;
  }

  // Determine bare path
  git::BareManager bare_mgr(ectx.store, ectx.log);
  std::string project_name = fs::path(cwd).filename().string();
  std::string bare_path;

  if (ctx.argc >= 4) {
    // User specified path
    bare_path = ctx.argv[3];
  } else {
    // Use default
    bare_path = bare_mgr.default_bare_path(project_name);
  }

  // Create bare repo
  if (!bare_mgr.create_bare(bare_path)) {
    std::cerr << "Failed to create bare repository\n";
    return 1;
  }

  std::cout << "Created bare repository: " << bare_path << "\n";

  // Link to bare as origin
  if (!bare_mgr.link_to_bare(cwd, bare_path)) {
    std::cerr << "Failed to link to bare repository\n";
    return 1;
  }

  std::cout << "Linked to bare repository as origin\n";

  // Push to bare
  if (client.has_commits()) {
    std::cout << "Pushing to bare repository...\n";
    if (client.push("origin", "")) {
      std::cout << "Pushed successfully\n";
    } else {
      std::cerr << "Push failed\n";
      return 1;
    }
  }

  return 0;
}

// nazg git add - Stage files for commit
static int cmd_git_add(const directive::command_context &ctx,
                       const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository\n";
    return 1;
  }

  // If no files specified or just flags, add all
  if (ctx.argc < 3) {
    if (!client.add_all()) {
      std::cerr << "Failed to stage changes\n";
      return 1;
    }
    std::cout << "Staged all changes\n";
    return 0;
  }

  // Check for help
  if (std::string(ctx.argv[2]) == "--help" || std::string(ctx.argv[2]) == "-h") {
    std::cout << "Usage: nazg git add [files...]\n\n";
    std::cout << "Stage files for commit\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nazg git add                 # Stage all changes\n";
    std::cout << "  nazg git add file.txt        # Stage specific file\n";
    std::cout << "  nazg git add .               # Stage all in current dir\n";
    return 0;
  }

  // Add specific files
  std::vector<std::string> files;
  for (int i = 2; i < ctx.argc; ++i) {
    files.push_back(ctx.argv[i]);
  }

  if (!client.add(files)) {
    std::cerr << "Failed to stage files\n";
    return 1;
  }

  std::cout << "Staged " << files.size() << " file(s)\n";
  return 0;
}

// nazg git clone - Clone a repository
static int cmd_git_clone(const directive::command_context &ctx,
                         const directive::context &/*ectx*/) {
  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg git clone <url> [directory]\n";
    return 1;
  }

  if (std::string(ctx.argv[2]) == "--help" || std::string(ctx.argv[2]) == "-h") {
    std::cout << "Usage: nazg git clone <url> [directory]\n\n";
    std::cout << "Clone a git repository\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nazg git clone git@github.com:user/repo.git\n";
    std::cout << "  nazg git clone /path/to/bare/repo.git myproject\n";
    return 0;
  }

  std::string url = ctx.argv[2];
  std::string target_dir;

  if (ctx.argc >= 4) {
    target_dir = ctx.argv[3];
  }

  // Build and execute git clone command
  std::string cmd = "git clone " + nazg::system::shell_quote(url);
  if (!target_dir.empty()) {
    cmd += " " + nazg::system::shell_quote(target_dir);
  }

  int result = ::system(cmd.c_str());
  if (result != 0) {
    std::cerr << "Failed to clone repository\n";
    return 1;
  }

  std::cout << "✓ Cloned repository\n";
  return 0;
}

// nazg git push - Push commits to remote
static int cmd_git_push(const directive::command_context &ctx,
                        const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository\n";
    return 1;
  }

  if (ctx.argc > 2 && (std::string(ctx.argv[2]) == "--help" || std::string(ctx.argv[2]) == "-h")) {
    std::cout << "Usage: nazg git push [options] [remote] [refspec]\n\n";
    std::cout << "Push commits to remote repository\n\n";
    std::cout << "Options:\n";
    std::cout << "  -u, --set-upstream    Set upstream tracking\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nazg git push                  # Push current branch\n";
    std::cout << "  nazg git push origin main      # Push main to origin\n";
    std::cout << "  nazg git push -u origin HEAD   # Push and set upstream\n";
    return 0;
  }

  // Build git push command - don't quote flags or simple refs
  std::ostringstream cmd;
  cmd << "git push";

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    // Don't quote flags or common refspecs
    if (arg[0] == '-' || arg == "HEAD" || arg.find('/') == std::string::npos) {
      cmd << " " << arg;
    } else {
      cmd << " " << nazg::system::shell_quote(arg);
    }
  }

  auto result = nazg::system::run_command_capture(cmd.str());
  if (result.exit_code != 0) {
    std::cerr << result.output;
    std::cerr << "Failed to push\n";
    return 1;
  }

  std::cout << result.output;
  std::cout << "✓ Pushed successfully\n";
  return 0;
}

// nazg git commit - Create commit
static int cmd_git_commit(const directive::command_context &ctx,
                          const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository\n";
    return 1;
  }

  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg git commit [options]\n";
    std::cerr << "  -m <msg>    Commit message\n";
    std::cerr << "  -a          Automatically stage modified files\n";
    std::cerr << "  -am <msg>   Stage all and commit\n";
    return 1;
  }

  // Check for help
  if (ctx.argc > 2 && (std::string(ctx.argv[2]) == "--help" || std::string(ctx.argv[2]) == "-h")) {
    std::cout << "Usage: nazg git commit [options]\n\n";
    std::cout << "Create a commit\n\n";
    std::cout << "Options:\n";
    std::cout << "  -m <msg>       Use given message\n";
    std::cout << "  -a             Stage modified files\n";
    std::cout << "  -am <msg>      Stage all and commit\n";
    return 0;
  }

  // Build git commit command by parsing arguments carefully
  // Note: forward_git_command shifts argv, so actual args start at index 2
  std::vector<std::string> args;

  if (ectx.log) {
    ectx.log->debug("git.commit", "Parsing " + std::to_string(ctx.argc) + " arguments");
  }

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];

    // Check if this is -m or -am followed by message
    if (arg == "-m" || arg == "-am") {
      args.push_back(arg);
      // Next argument is the message
      if (i + 1 < ctx.argc) {
        i++;
        std::string msg = ctx.argv[i];
        args.push_back(msg);
        if (ectx.log) {
          ectx.log->debug("git.commit", "Found message flag with message");
        }
      }
    } else if (arg == "-a") {
      args.push_back(arg);
    } else {
      // Other arguments
      args.push_back(arg);
    }
  }

  // Build command string
  std::ostringstream cmd;
  cmd << "git commit";

  for (size_t i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];

    // Check if previous arg was -m or -am, then this is the message
    if (i > 0 && (args[i-1] == "-m" || args[i-1] == "-am")) {
      // This is a commit message, quote it
      cmd << " " << nazg::system::shell_quote(arg);
    } else if (arg[0] == '-') {
      // This is a flag, don't quote
      cmd << " " << arg;
    } else {
      // Other arguments
      cmd << " " << nazg::system::shell_quote(arg);
    }
  }

  std::string cmd_str = cmd.str();
  if (ectx.log) {
    ectx.log->debug("git.commit", "Executing: " + cmd_str);
  }

  auto result = nazg::system::run_command_capture(cmd_str);
  if (result.exit_code != 0) {
    std::cerr << result.output;
    std::cerr << "Failed to create commit\n";
    return 1;
  }

  std::cout << result.output;
  return 0;
}

// nazg::git remote - Manage remotes
static int cmd_git_remote(const directive::command_context &ctx,
                          const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository\n";
    return 1;
  }

  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg git remote <subcommand> [args]\n";
    std::cerr << "  add <name> <url>    Add a remote\n";
    std::cerr << "  remove <name>       Remove a remote\n";
    std::cerr << "  -v                  List remotes\n";
    return 1;
  }

  // Build git remote command - don't quote flags or subcommands
  std::ostringstream cmd;
  cmd << "git remote";

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    // Don't quote flags, subcommands, or remote names (simple words)
    if (arg[0] == '-' || arg == "add" || arg == "remove" || arg == "rename" ||
        arg == "get-url" || arg == "set-url" || arg == "show" ||
        (arg.find('/') == std::string::npos && arg.find('@') == std::string::npos)) {
      cmd << " " << arg;
    } else {
      // Quote URLs and paths
      cmd << " " << nazg::system::shell_quote(arg);
    }
  }

  auto result = nazg::system::run_command_capture(cmd.str());
  if (result.exit_code != 0) {
    std::cerr << result.output;
    return 1;
  }

  std::cout << result.output;
  return 0;
}

// nazg git checkout - Switch branches or restore files
static int cmd_git_checkout(const directive::command_context &ctx,
                             const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository\n";
    return 1;
  }

  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg git checkout <branch>\n";
    return 1;
  }

  if (std::string(ctx.argv[2]) == "--help" || std::string(ctx.argv[2]) == "-h") {
    std::cout << "Usage: nazg git checkout [options] <branch>\n\n";
    std::cout << "Switch branches or restore working tree files\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nazg git checkout main       # Switch to main branch\n";
    std::cout << "  nazg git checkout -b feature # Create and switch to feature\n";
    return 0;
  }

  // Build git checkout command - don't quote flags or branch names
  std::ostringstream cmd;
  cmd << "git checkout";

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    // Don't quote flags or simple branch/file names
    if (arg[0] == '-' || arg.find('/') == std::string::npos) {
      cmd << " " << arg;
    } else {
      cmd << " " << nazg::system::shell_quote(arg);
    }
  }

  auto result = nazg::system::run_command_capture(cmd.str());
  if (result.exit_code != 0) {
    std::cerr << result.output;
    return 1;
  }

  std::cout << result.output;
  return 0;
}

// nazg git sync - Push to all remotes
static int cmd_git_sync(const directive::command_context &/*ctx*/,
                        const directive::context &ectx) {
  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  if (!client.is_repo()) {
    std::cerr << "Not a git repository\n";
    return 1;
  }

  if (!client.has_commits()) {
    std::cerr << "No commits to push\n";
    return 1;
  }

  auto remotes = client.list_remotes();
  if (remotes.empty()) {
    std::cerr << "No remotes configured\n";
    return 1;
  }

  std::cout << "Syncing to " << remotes.size() << " remote(s)...\n";
  bool all_ok = true;
  for (const auto &[name, url] : remotes) {
    std::cout << "Pushing to " << name << "...\n";
    if (client.push(name, "")) {
      std::cout << "  ✓ " << name << "\n";
    } else {
      std::cerr << "  ✗ " << name << " (failed)\n";
      all_ok = false;
    }
  }

  return all_ok ? 0 : 1;
}

// nazg git server add <label>|<type host> - Register git server
static int cmd_git_server_add(const directive::command_context &ctx,
                               const directive::context &ectx) {
  auto print_usage = [](const std::string &prog) {
    std::cerr << "Usage:\n";
    std::cerr << "  " << prog << " git server add <label>\n";
    std::cerr << "  " << prog << " git server add --type TYPE --host HOST [--ssh-user USER]\n";
    std::cerr << "\nExamples:\n";
    std::cerr << "  " << prog << " git server add devbox\n";
    std::cerr << "  " << prog << " git server add cgit 10.0.0.4 --ssh-user director\n";
    std::cerr << std::endl;
  };

  if (!ectx.store) {
    std::cerr << "Error: Nexus store not available\n";
    return 1;
  }

  git::ServerRegistry registry(ectx.cfg, ectx.store, ectx.log);

  std::string label;
  std::string type;
  std::string host_arg;
  std::string ssh_user_flag;
  int ssh_port = 22;
  std::string repo_path = "/srv/git";
  std::vector<std::string> positionals;

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if ((arg == "--help" || arg == "-h")) {
      print_usage(ctx.argv[0]);
      return 0;
    } else if (arg == "--label" && i + 1 < ctx.argc) {
      label = ctx.argv[++i];
    } else if (arg == "--type" && i + 1 < ctx.argc) {
      type = to_lower_copy(ctx.argv[++i]);
    } else if (arg == "--host" && i + 1 < ctx.argc) {
      host_arg = ctx.argv[++i];
    } else if (arg == "--ssh-user" && i + 1 < ctx.argc) {
      ssh_user_flag = ctx.argv[++i];
    } else if (arg == "--ssh-port" && i + 1 < ctx.argc) {
      ssh_port = std::atoi(ctx.argv[++i]);
    } else if (arg == "--repo-path" && i + 1 < ctx.argc) {
      repo_path = ctx.argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n\n";
      print_usage(ctx.argv[0]);
      return 1;
    } else {
      positionals.push_back(arg);
    }
  }

  bool from_config = false;
  if (label.empty() && positionals.size() == 1 && type.empty() &&
      host_arg.empty()) {
    label = positionals.front();
    from_config = true;
  } else {
    if (!positionals.empty() && type.empty()) {
      type = to_lower_copy(positionals[0]);
    }
    if (positionals.size() >= 2 && host_arg.empty()) {
      host_arg = positionals[1];
    }
    if (positionals.size() >= 3 && label.empty()) {
      label = positionals[2];
    }
  }

  git::ServerConfig cfg;

  if (from_config) {
    auto entry = registry.get_server(label);
    if (!entry) {
      std::cerr << "Server '" << label
                << "' not found in config.toml. Define [git.servers."
                << label << "] first.\n";
      return 1;
    }
    cfg = entry->config;
    type = cfg.type;
    host_arg = cfg.host;
    ssh_port = cfg.ssh_port;
    ssh_user_flag = cfg.ssh_user;
    repo_path = cfg.repo_base_path;
  } else {
    if (!positionals.empty() && host_arg.empty() && positionals.size() >= 2) {
      host_arg = positionals[1];
    }

    if (type.empty()) {
      std::cerr << "Error: server type required (e.g., --type cgit)\n\n";
      print_usage(ctx.argv[0]);
      return 1;
    }

    if (host_arg.empty()) {
      std::cerr << "Error: host required (e.g., --host 10.0.0.4)\n\n";
      print_usage(ctx.argv[0]);
      return 1;
    }

    cfg.type = to_lower_copy(type);
    std::string parsed_host = host_arg;
    std::string parsed_user;
    auto at_pos = parsed_host.find('@');
    if (at_pos != std::string::npos) {
      parsed_user = parsed_host.substr(0, at_pos);
      parsed_host = parsed_host.substr(at_pos + 1);
    }

    cfg.host = parsed_host;
    cfg.ssh_user = !ssh_user_flag.empty()
                       ? ssh_user_flag
                       : (!parsed_user.empty() ? parsed_user : "git");
    cfg.ssh_port = ssh_port;
    cfg.repo_base_path = repo_path;

    if (cfg.type == "cgit") {
      cfg.port = 80;
      cfg.config_path = "/etc/cgitrc";
      cfg.web_url = "http://" + cfg.host + "/cgit";
    } else if (cfg.type == "gitea") {
      cfg.port = 3000;
      cfg.config_path = "/etc/gitea/app.ini";
      cfg.repo_base_path = "/var/lib/gitea/repositories";
      cfg.web_url = "http://" + cfg.host + ":3000";
    } else {
      cfg.port = 80;
    }
  }

  if (cfg.host.empty()) {
    std::cerr << "Error: host cannot be empty\n";
    return 1;
  }

  if (cfg.ssh_user.empty()) {
    cfg.ssh_user = "git";
  }

  if (label.empty()) {
    label = cfg.host;
  }

  if (!registry.add_server(label, cfg)) {
    std::cerr << "Failed to register server '" << label << "'\n";
    return 1;
  }

  std::cout << "Registered git server '" << label << "' (" << cfg.type
            << ") at " << cfg.host << "\n";

  try {
    auto server = git::create_server(cfg, ectx.store, ectx.log);
    auto status = server->get_status();

    if (status.reachable) {
      std::cout << "✓ SSH reachable as " << cfg.ssh_user << "@" << cfg.host
                << ":" << cfg.ssh_port << "\n";
      if (status.installed) {
        std::cout << "✓ " << cfg.type << " already installed";
        if (!status.version.empty()) {
          std::cout << " (version: " << status.version << ")";
        }
        std::cout << "\n";
        if (status.repo_count > 0) {
          std::cout << "  Repositories detected: " << status.repo_count << "\n";
        }
        std::cout << "\nNext steps:\n";
        std::cout << "  nazg bot git-doctor --host " << cfg.ssh_user << "@"
                  << cfg.host << "\n";
        std::cout << "  nazg git server status " << label << "\n";
      } else {
        std::cout << "⚠ " << cfg.type << " not installed yet\n";
        std::cout << "\nInstall with:\n";
        std::cout << "  nazg git server install " << label << "\n";
      }
    } else {
      std::cerr << "✗ Cannot reach server: " << status.error_message << "\n";
      std::cerr << "  Test manually: ssh " << cfg.ssh_user << "@" << cfg.host
                << "\n";
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

// nazg git-server-status - Show server status
static int cmd_git_server_status(const directive::command_context &ctx,
                                  const directive::context &ectx) {
  auto print_usage = [](const std::string &prog) {
    std::cerr << "Usage: " << prog << " git server status <label|host> [--type TYPE] [--health-check]\n";
    std::cerr << "       " << prog << " git server status --label LABEL [--health-check]\n";
    std::cerr << std::endl;
  };

  if (!ectx.store) {
    std::cerr << "Error: Nexus store not available\n";
    return 1;
  }

  git::ServerRegistry registry(ectx.cfg, ectx.store, ectx.log);

  std::string label;
  std::string host_arg;
  std::string type_override;
  bool skip_sync_prompt = false;
  bool health_check_requested = false;
  std::vector<std::string> positionals;

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(ctx.argv[0]);
      return 0;
    } else if (arg == "--label" && i + 1 < ctx.argc) {
      label = ctx.argv[++i];
    } else if (arg == "--host" && i + 1 < ctx.argc) {
      host_arg = ctx.argv[++i];
    } else if (arg == "--type" && i + 1 < ctx.argc) {
      type_override = to_lower_copy(ctx.argv[++i]);
    } else if (arg == "--no-sync") {
      skip_sync_prompt = true;
    } else if (arg == "--health-check") {
      health_check_requested = true;
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n\n";
      print_usage(ctx.argv[0]);
      return 1;
    } else {
      positionals.push_back(arg);
    }
  }

  if (label.empty() && !positionals.empty()) {
    if (positionals[0].find('@') != std::string::npos) {
      host_arg = positionals[0];
    } else {
      label = positionals[0];
    }
  }

  git::ServerConfig cfg;
  bool from_registry = false;
  std::optional<git::ServerEntry> entry;

  if (!label.empty()) {
    entry = registry.get_server(label);
    if (entry) {
      cfg = entry->config;
      from_registry = true;

      if (entry->has_config_changes && !skip_sync_prompt) {
        prompt::Prompt p(ectx.log);
        if (registry.sync_config_to_database(label, &p)) {
          entry = registry.get_server(label);
          if (entry) {
            cfg = entry->config;
          }
        }
      }
    } else if (host_arg.empty()) {
      std::cerr << "Server '" << label << "' not found. Define it in config.toml or use --host.\n";
      return 1;
    }
  }

  if (!from_registry) {
    if (host_arg.empty()) {
      std::cerr << "Error: host or label required\n\n";
      print_usage(ctx.argv[0]);
      return 1;
    }

    std::string parsed_host = host_arg;
    std::string parsed_user;
    auto at_pos = parsed_host.find('@');
    if (at_pos != std::string::npos) {
      parsed_user = parsed_host.substr(0, at_pos);
      parsed_host = parsed_host.substr(at_pos + 1);
    }

    cfg.host = parsed_host;
    cfg.type = type_override.empty() ? "cgit" : to_lower_copy(type_override);
    cfg.ssh_user = parsed_user.empty() ? "git" : parsed_user;
    cfg.ssh_port = 22;
    cfg.repo_base_path = "/srv/git";

    if (cfg.type == "cgit") {
      cfg.port = 80;
      cfg.config_path = "/etc/cgitrc";
      cfg.web_url = "http://" + cfg.host + "/cgit";
    } else if (cfg.type == "gitea") {
      cfg.port = 3000;
      cfg.config_path = "/etc/gitea/app.ini";
      cfg.web_url = "http://" + cfg.host + ":3000";
    } else {
      cfg.port = 80;
    }
  }

  if (cfg.host.empty()) {
    std::cerr << "Error: host cannot be empty\n";
    return 1;
  }

  std::string effective_label = from_registry ? label : std::string();

  try {
    auto server = git::create_server(cfg, ectx.store, ectx.log);
    auto status = server->get_status();

    std::cout << "Git Server Status\n";
    std::cout << "─────────────────\n";
    std::cout << "Label:     " << (effective_label.empty() ? "(ad-hoc)" : effective_label) << "\n";
    std::cout << "Type:      " << status.type << "\n";
    std::cout << "Host:      " << cfg.host << "\n";
    std::cout << "SSH:       " << cfg.ssh_user << "@" << cfg.host << ":" << cfg.ssh_port << "\n";
    std::cout << "Reachable: " << (status.reachable ? "yes" : "no") << "\n";

    if (status.reachable) {
      std::cout << "Installed: " << (status.installed ? "yes" : "no") << "\n";
      if (status.installed) {
        if (!status.version.empty()) {
          std::cout << "Version:  " << status.version << "\n";
        }
        std::cout << "Repos:    " << status.repo_count << "\n";
        if (!cfg.web_url.empty()) {
          std::cout << "Web UI:   " << cfg.web_url << "\n";
        }
      }
    } else {
      std::cout << "Error:     " << status.error_message << "\n";
    }

    if (health_check_requested) {
      std::cout << "\nHealth Snapshot\n";
      if (!from_registry || !entry || entry->id == 0) {
        std::cout << "  No recorded health data (server not registered).\n";
      } else {
        auto records = ectx.store->list_git_server_health(entry->id, 1);
        if (records.empty()) {
          std::cout << "  No health records found. Run 'nazg bot git-doctor --host "
                    << cfg.ssh_user << "@" << cfg.host
                    << "' to capture diagnostics.\n";
        } else {
          const auto &health = records.front();
          std::cout << "  Status:         " << health.status << "\n";
          std::cout << "  Web UI:         " << (health.web_ui_reachable ? "reachable" : "unreachable") << "\n";
          std::cout << "  HTTP clone:     " << (health.http_clone_works ? "ok" : "fail") << "\n";
          std::cout << "  SSH push:       " << (health.ssh_push_works ? "ok" : "fail") << "\n";
          if (health.repo_count > 0) {
            std::cout << "  Repositories:   " << health.repo_count << "\n";
          }
          if (health.total_size_bytes > 0) {
            std::cout << "  Total size:     " << (health.total_size_bytes / (1024 * 1024)) << " MB\n";
          }
          if (health.disk_used_pct > 0) {
            std::cout << "  Disk used:      " << health.disk_used_pct << "%\n";
          }
          if (!health.notes_json.empty()) {
            std::cout << "  Notes:          " << health.notes_json << "\n";
          }
        }
      }
    }

    if (from_registry) {
      if (status.installed) {
        registry.mark_installed(label);
      }
      const char *state = status.reachable
                              ? (status.installed ? "online" : "not_installed")
                              : "offline";
      registry.update_status(label, state);
      registry.update_last_check(label, std::time(nullptr));
    }

    if (status.reachable && !status.installed) {
      std::cout << "\nInstall with:\n  nazg git server install "
                << (effective_label.empty() ? cfg.host : effective_label)
                << "\n";
    }

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

// nazg git-server-install - Install git server software
static int cmd_git_server_install(const directive::command_context &ctx,
                                   const directive::context &ectx) {
  auto flags = parse_prompt_flags(ctx, ectx);
  auto print_usage = [](const std::string &prog) {
    std::cerr << "Usage: " << prog << " git server install <label|host> [options]\n";
    std::cerr << "Options:\n  --label LABEL       Use server defined in config.toml\n"
                 "  --host HOST         Connect to host (supports user@host)\n"
                 "  --type TYPE         Server type when using --host\n"
                 "  --ssh-user USER     Override SSH user\n"
                 "  --ssh-port PORT     Override SSH port\n"
                 "  --repo-path PATH    Override repo base path\n";
    std::cerr << std::endl;
  };

  if (!ectx.store) {
    std::cerr << "Error: Nexus store not available\n";
    return 1;
  }

  git::ServerRegistry registry(ectx.cfg, ectx.store, ectx.log);

  std::string label;
  std::string host_arg;
  std::string type_override;
  std::string ssh_user_override;
  int ssh_port_override = -1;
  std::string repo_path_override;
  std::vector<std::string> positionals;

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage(ctx.argv[0]);
      return 0;
    } else if (arg == "--label" && i + 1 < ctx.argc) {
      label = ctx.argv[++i];
    } else if (arg == "--host" && i + 1 < ctx.argc) {
      host_arg = ctx.argv[++i];
    } else if (arg == "--type" && i + 1 < ctx.argc) {
      type_override = to_lower_copy(ctx.argv[++i]);
    } else if (arg == "--ssh-user" && i + 1 < ctx.argc) {
      ssh_user_override = ctx.argv[++i];
    } else if (arg == "--ssh-port" && i + 1 < ctx.argc) {
      ssh_port_override = std::atoi(ctx.argv[++i]);
    } else if (arg == "--repo-path" && i + 1 < ctx.argc) {
      repo_path_override = ctx.argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n\n";
      print_usage(ctx.argv[0]);
      return 1;
    } else {
      positionals.push_back(arg);
    }
  }

  if (label.empty() && host_arg.empty() && !positionals.empty()) {
    if (positionals[0].find('@') != std::string::npos ||
        positionals[0].find('.') != std::string::npos) {
      host_arg = positionals[0];
    } else {
      label = positionals[0];
    }
  }

  git::ServerConfig cfg;
  bool from_registry = false;

  if (!label.empty()) {
    auto entry = registry.get_server(label);
    if (entry) {
      cfg = entry->config;
      from_registry = true;
    } else if (host_arg.empty()) {
      std::cerr << "Server '" << label
                << "' not found. Define it or provide --host.\n";
      return 1;
    }
  }

  if (!from_registry) {
    if (host_arg.empty()) {
      std::cerr << "Error: host required for installation\n\n";
      print_usage(ctx.argv[0]);
      return 1;
    }

    std::string parsed_host = host_arg;
    std::string parsed_user;
    auto at_pos = parsed_host.find('@');
    if (at_pos != std::string::npos) {
      parsed_user = parsed_host.substr(0, at_pos);
      parsed_host = parsed_host.substr(at_pos + 1);
    }

    cfg.host = parsed_host;
    cfg.type = type_override.empty() ? "cgit" : to_lower_copy(type_override);
    cfg.ssh_user = !ssh_user_override.empty()
                      ? ssh_user_override
                      : (!parsed_user.empty() ? parsed_user : "director");
    cfg.ssh_port = ssh_port_override > 0 ? ssh_port_override : 22;
    cfg.repo_base_path = repo_path_override.empty() ? "/srv/git" : repo_path_override;

    if (cfg.type == "cgit") {
      cfg.port = 80;
      cfg.config_path = "/etc/cgitrc";
      cfg.web_url = "http://" + cfg.host + "/cgit";
    } else if (cfg.type == "gitea") {
      cfg.port = 3000;
      cfg.config_path = "/etc/gitea/app.ini";
      cfg.repo_base_path = repo_path_override.empty() ? "/var/lib/gitea/repositories"
                                                     : repo_path_override;
      cfg.web_url = "http://" + cfg.host + ":3000";
    } else {
      cfg.port = 80;
    }
  } else {
    if (!ssh_user_override.empty()) {
      cfg.ssh_user = ssh_user_override;
    }
    if (ssh_port_override > 0) {
      cfg.ssh_port = ssh_port_override;
    }
    if (!repo_path_override.empty()) {
      cfg.repo_base_path = repo_path_override;
    }
  }

  if (cfg.host.empty()) {
    std::cerr << "Error: host cannot be empty\n";
    return 1;
  }

  if (cfg.ssh_user.empty()) {
    cfg.ssh_user = "director";
  }

  std::string effective_label = from_registry ? label : std::string();
  if (cfg.type == "gitea") {
    if (cfg.config_path.empty()) {
      cfg.config_path = "/etc/gitea/app.ini";
    } else if (cfg.config_path.find('.') == std::string::npos) {
      if (cfg.config_path.back() == '/') {
        cfg.config_path += "app.ini";
      } else {
        cfg.config_path += "/app.ini";
      }
    }
  }
  if (!effective_label.empty()) {
    registry.add_server(effective_label, cfg);
  }

  prompt::Prompt confirm_prompt(ectx.log);
  confirm_prompt.title("git-server-install")
                .style(flags.style)
                .force_yes(flags.force_yes)
                .force_no(flags.force_no)
                .question("Install " + cfg.type + " on remote server?");

  confirm_prompt.fact("Server", cfg.host)
                .fact("Type", cfg.type)
                .fact("SSH", cfg.ssh_user + "@" + cfg.host + ":" + std::to_string(cfg.ssh_port))
                .fact("Repo path", cfg.repo_base_path);

  confirm_prompt.action("Install packages and dependencies")
                .action("Configure web server and services")
                .action("Create repository root at " + cfg.repo_base_path)
                .action("Enable and start services");

  confirm_prompt.warning("This will run sudo commands on the remote server")
                .warning("Requires SSH access with sudo privileges");

  bool confirmed = confirm_prompt.confirm(true);

  if (!confirmed) {
    std::cout << "Cancelled.\n";
    return 0;
  }

  try {
    if (ectx.log) {
      ectx.log->info("Git::server-install", "Starting installation of " + cfg.type + " on " + cfg.host);
    }

    auto server = git::create_server(cfg, ectx.store, ectx.log);

    if (server->install()) {
      std::string admin_password;
      bool have_admin_password = false;
      bool token_stored = false;

      if (auto gitea = dynamic_cast<git::GiteaServer*>(server.get())) {
        if (!gitea->admin_password().empty()) {
          admin_password = gitea->admin_password();
          have_admin_password = true;
        }

        if (!effective_label.empty() && !gitea->admin_token().empty()) {
          registry.update_admin_token(effective_label, gitea->admin_token());
          token_stored = true;
        }
      }

      if (flags.style == prompt::Style::MINIMAL) {
        std::cout << "✓ Installation complete - " << cfg.web_url << "\n";
        if (have_admin_password) {
          std::cout << "Admin user: admin\n"
                    << "Admin password: " << admin_password << "\n";
        }
        if (token_stored && !effective_label.empty()) {
          std::cout << "API token stored for " << effective_label << "\n";
        }
      } else {
        std::cout << "\n";
        std::cout << "┌────────────────────────────────────────────────────────────\n";
        std::cout << "│ ✓ Installation complete!\n";
        std::cout << "│\n";
        std::cout << "│ Server: " << cfg.host << "\n";
        std::cout << "│ Type: " << cfg.type << "\n";
        std::cout << "│ Web UI: " << cfg.web_url << "\n";
        std::cout << "└────────────────────────────────────────────────────────────\n";
        if (have_admin_password) {
          std::cout << "  Admin user: admin\n";
          std::cout << "  Admin password: " << admin_password << "\n";
        }
        if (token_stored) {
          std::cout << "  API token stored securely for future operations\n";
        }
      }
      if (!effective_label.empty()) {
        registry.mark_installed(effective_label);
        registry.update_status(effective_label, "online");
        registry.update_last_check(effective_label, std::time(nullptr));
        if (token_stored && ectx.log) {
          ectx.log->info("Git/registry", "Stored admin token for " + effective_label);
        }
      }
      return 0;
    }

    std::cerr << "✗ Installation failed\n";
    if (!effective_label.empty()) {
      registry.update_status(effective_label, "offline");
    }
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    if (!effective_label.empty()) {
      registry.update_status(effective_label, "offline");
    }
    return 1;
  }
}

// nazg git-server-sync - Sync repos to server
static int cmd_git_server_sync(const directive::command_context &ctx,
                                const directive::context &ectx) {
  auto flags = parse_prompt_flags(ctx, ectx);

  if (!ectx.store) {
    std::cerr << "Error: Nexus store not available\n";
    return 1;
  }

  git::ServerRegistry registry(ectx.cfg, ectx.store, ectx.log);

  std::string label;
  std::string host_arg;
  std::string type_override;
  std::string ssh_user_override;
  std::string source_path = "/mnt/nas/repos";
  std::vector<std::string> positionals;

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << ctx.argv[0] << " git server sync <label|host> [--source PATH]\n";
      return 0;
    } else if (arg == "--label" && i + 1 < ctx.argc) {
      label = ctx.argv[++i];
    } else if (arg == "--host" && i + 1 < ctx.argc) {
      host_arg = ctx.argv[++i];
    } else if (arg == "--type" && i + 1 < ctx.argc) {
      type_override = to_lower_copy(ctx.argv[++i]);
    } else if (arg == "--ssh-user" && i + 1 < ctx.argc) {
      ssh_user_override = ctx.argv[++i];
    } else if (arg == "--source" && i + 1 < ctx.argc) {
      source_path = ctx.argv[++i];
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n";
      return 1;
    } else {
      positionals.push_back(arg);
    }
  }

  if (label.empty() && host_arg.empty() && !positionals.empty()) {
    if (positionals[0].find('@') != std::string::npos ||
        positionals[0].find('.') != std::string::npos) {
      host_arg = positionals[0];
    } else {
      label = positionals[0];
    }
  }

  git::ServerConfig cfg;
  bool from_registry = false;

  if (!label.empty()) {
    auto entry = registry.get_server(label);
    if (entry) {
      cfg = entry->config;
      from_registry = true;
    } else if (host_arg.empty()) {
      std::cerr << "Server '" << label << "' not found." << std::endl;
      return 1;
    }
  }

  if (!from_registry) {
    if (host_arg.empty()) {
      std::cerr << "Error: host required for sync" << std::endl;
      return 1;
    }

    std::string parsed_host = host_arg;
    std::string parsed_user;
    auto at_pos = parsed_host.find('@');
    if (at_pos != std::string::npos) {
      parsed_user = parsed_host.substr(0, at_pos);
      parsed_host = parsed_host.substr(at_pos + 1);
    }

    cfg.host = parsed_host;
    cfg.type = type_override.empty() ? "cgit" : to_lower_copy(type_override);
    cfg.ssh_user = !ssh_user_override.empty()
                      ? ssh_user_override
                      : (!parsed_user.empty() ? parsed_user : "git");
    cfg.ssh_port = 22;
    cfg.repo_base_path = "/srv/git";
  } else if (!ssh_user_override.empty()) {
    cfg.ssh_user = ssh_user_override;
  }

  if (cfg.host.empty()) {
    std::cerr << "Error: host cannot be empty" << std::endl;
    return 1;
  }

  git::MigrationPlanner planner(ectx.store, ectx.log);
  auto repos = planner.scan_local_repos(source_path);

  if (repos.empty()) {
    std::cout << "No bare repositories found in " << source_path << "\n";
    std::cout << "Hint: Use 'nazg git create-bare' to create bare repos\n";
    return 0;
  }

  prompt::Prompt confirm_prompt(ectx.log);
  confirm_prompt.title("git-server-sync")
                .style(flags.style)
                .force_yes(flags.force_yes)
                .force_no(flags.force_no)
                .question("Sync " + std::to_string(repos.size()) +
                          " repository(s) to remote server?");

  confirm_prompt.fact("Server", cfg.host)
                .fact("Target path", cfg.repo_base_path)
                .fact("SSH", cfg.ssh_user + "@" + cfg.host)
                .fact("Repo count", std::to_string(repos.size()))
                .fact("Source", source_path);

  confirm_prompt.info("Repositories to sync:");
  for (const auto &repo : repos) {
    confirm_prompt.detail("  • " + repo.name);
  }

  confirm_prompt.action("Upload repositories via rsync over SSH")
                .action("Update cgit configuration")
                .action("Scan repositories on server");

  bool confirmed = confirm_prompt.confirm(true);
  if (!confirmed) {
    std::cout << "Cancelled.\n";
    return 0;
  }

  const std::string server_label = from_registry ? label : cfg.host;
  std::vector<std::string> repo_paths;
  repo_paths.reserve(repos.size());
  std::vector<int64_t> migration_ids(repos.size(), 0);

  try {
    auto server = git::create_server(cfg, ectx.store, ectx.log);

    for (std::size_t i = 0; i < repos.size(); ++i) {
      const auto &repo = repos[i];
      auto full_path = (fs::path(source_path) / repo.name).string();
      repo_paths.push_back(full_path);
      migration_ids[i] = planner.record_migration_start(server_label, repo, full_path, 0);
    }

    if (server->sync_repos(repo_paths)) {
      for (auto migration_id : migration_ids) {
        planner.record_migration_success(migration_id, server_label);
      }
      if (from_registry) {
        registry.update_status(label, "online");
        registry.update_last_check(label, std::time(nullptr));
      }

      if (flags.style == prompt::Style::MINIMAL) {
        std::cout << "✓ Synced " << repos.size() << " repo(s)\n";
      } else {
        std::cout << "\n";
        std::cout << "┌────────────────────────────────────────────────────────────\n";
        std::cout << "│ ✓ Sync complete!\n";
        std::cout << "│\n";
        std::cout << "│ Synced: " << repos.size() << " repository(s)\n";
        std::cout << "│ Server: " << cfg.host << "\n";
        std::cout << "└────────────────────────────────────────────────────────────\n";
      }
      return 0;
    }

    for (auto migration_id : migration_ids) {
      planner.record_migration_failure(migration_id, server_label, "sync failed");
    }
    std::cerr << "✗ Sync failed\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    for (auto migration_id : migration_ids) {
      planner.record_migration_failure(migration_id, server_label, e.what());
    }
    return 1;
  }
}

// nazg git-config - Interactive git configuration manager
static int cmd_git_config(const directive::command_context &ctx,
                          const directive::context &ectx) {
  auto flags = parse_prompt_flags(ctx, ectx, 2);  // Start at index 2 for git-config
  bool global = true;  // Default to global scope
  bool show_only = false;

  // Parse arguments (start at 2 because ctx.argv[0]=program, ctx.argv[1]=command)
  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--local") {
      global = false;
    } else if (arg == "--global") {
      global = true;
    } else if (arg == "--show") {
      show_only = true;
    }
  }

  std::string cwd = get_cwd();
  git::Client client(cwd, ectx.log);

  // Get current configuration
  auto cfg = client.get_full_config(global);

  // ── Show current configuration ──
  prompt::Prompt view_prompt(ectx.log);
  view_prompt.title("git-config")
             .style(flags.style);

  std::string scope_name = global ? "Global" : "Local (repository)";
  view_prompt.question("Git Configuration (" + scope_name + ")");

  view_prompt.fact("Scope", scope_name);

  // Show current values
  if (cfg.user_name) {
    view_prompt.fact("user.name", *cfg.user_name);
  } else {
    view_prompt.fact("user.name", "(not set)");
  }

  if (cfg.user_email) {
    view_prompt.fact("user.email", *cfg.user_email);
  } else {
    view_prompt.fact("user.email", "(not set)");
  }

  if (cfg.default_branch) {
    view_prompt.fact("init.defaultBranch", *cfg.default_branch);
  } else {
    view_prompt.fact("init.defaultBranch", "(not set)");
  }

  if (cfg.core_editor) {
    view_prompt.fact("core.editor", *cfg.core_editor);
  } else {
    view_prompt.fact("core.editor", "(not set)");
  }

  if (cfg.pull_rebase) {
    view_prompt.fact("pull.rebase", *cfg.pull_rebase);
  } else {
    view_prompt.fact("pull.rebase", "(not set)");
  }

  if (cfg.push_default) {
    view_prompt.fact("push.default", *cfg.push_default);
  } else {
    view_prompt.fact("push.default", "(not set)");
  }

  if (cfg.color_ui.has_value()) {
    view_prompt.fact("color.ui", *cfg.color_ui ? "true" : "false");
  } else {
    view_prompt.fact("color.ui", "(not set)");
  }

  if (show_only) {
    // Just display the configuration
    if (flags.style == prompt::Style::MINIMAL) {
      std::cout << "Git Config (" << scope_name << "):\n";
      std::cout << "  user.name: " << (cfg.user_name ? *cfg.user_name : "(not set)") << "\n";
      std::cout << "  user.email: " << (cfg.user_email ? *cfg.user_email : "(not set)") << "\n";
      std::cout << "  init.defaultBranch: " << (cfg.default_branch ? *cfg.default_branch : "(not set)") << "\n";
    } else {
      std::cout << "\n┌────────────────────────────────────────────────────────────\n";
      std::cout << "│ Git Configuration (" << scope_name << ")\n";
      std::cout << "├────────────────────────────────────────────────────────────\n";
      std::cout << "│ user.name: " << (cfg.user_name ? *cfg.user_name : "(not set)") << "\n";
      std::cout << "│ user.email: " << (cfg.user_email ? *cfg.user_email : "(not set)") << "\n";
      std::cout << "│ init.defaultBranch: " << (cfg.default_branch ? *cfg.default_branch : "(not set)") << "\n";
      std::cout << "│ core.editor: " << (cfg.core_editor ? *cfg.core_editor : "(not set)") << "\n";
      std::cout << "│ pull.rebase: " << (cfg.pull_rebase ? *cfg.pull_rebase : "(not set)") << "\n";
      std::cout << "│ push.default: " << (cfg.push_default ? *cfg.push_default : "(not set)") << "\n";
      std::cout << "│ color.ui: " << (cfg.color_ui.has_value() ? (*cfg.color_ui ? "true" : "false") : "(not set)") << "\n";
      std::cout << "└────────────────────────────────────────────────────────────\n";
    }
    return 0;
  }

  // ── Interactive configuration ──
  view_prompt.info("Configure git settings interactively");

  // Ask what to configure
  std::vector<std::string> options = {
    "Set user.name",
    "Set user.email",
    "Set init.defaultBranch",
    "Set core.editor",
    "Set pull.rebase",
    "Set push.default",
    "Toggle color.ui",
    "View all settings",
    "Done"
  };

  while (true) {
    prompt::Prompt menu_prompt(ectx.log);
    menu_prompt.title("git-config")
               .style(flags.style)
               .force_yes(flags.force_yes)
               .force_no(flags.force_no)
               .question("What would you like to configure?");

    int choice = menu_prompt.choice(options, 8);  // Default to "Done"

    if (choice == 8 || choice < 0) {
      // Done
      break;
    } else if (choice == 7) {
      // View all settings - refresh config
      cfg = client.get_full_config(global);
      std::cout << "\n┌────────────────────────────────────────────────────────────\n";
      std::cout << "│ Current Git Configuration (" << scope_name << ")\n";
      std::cout << "├────────────────────────────────────────────────────────────\n";
      std::cout << "│ user.name: " << (cfg.user_name ? *cfg.user_name : "(not set)") << "\n";
      std::cout << "│ user.email: " << (cfg.user_email ? *cfg.user_email : "(not set)") << "\n";
      std::cout << "│ init.defaultBranch: " << (cfg.default_branch ? *cfg.default_branch : "(not set)") << "\n";
      std::cout << "│ core.editor: " << (cfg.core_editor ? *cfg.core_editor : "(not set)") << "\n";
      std::cout << "│ pull.rebase: " << (cfg.pull_rebase ? *cfg.pull_rebase : "(not set)") << "\n";
      std::cout << "│ push.default: " << (cfg.push_default ? *cfg.push_default : "(not set)") << "\n";
      std::cout << "│ color.ui: " << (cfg.color_ui.has_value() ? (*cfg.color_ui ? "true" : "false") : "(not set)") << "\n";
      std::cout << "└────────────────────────────────────────────────────────────\n\n";
      continue;
    }

    // Get current value for the setting
    std::string key, current_value, placeholder;
    switch (choice) {
      case 0: // user.name
        key = "user.name";
        current_value = cfg.user_name ? *cfg.user_name : "";
        placeholder = "Your Name";
        break;
      case 1: // user.email
        key = "user.email";
        current_value = cfg.user_email ? *cfg.user_email : "";
        placeholder = "your.email@example.com";
        break;
      case 2: // init.defaultBranch
        key = "init.defaultBranch";
        current_value = cfg.default_branch ? *cfg.default_branch : "";
        placeholder = "main";
        break;
      case 3: // core.editor
        key = "core.editor";
        current_value = cfg.core_editor ? *cfg.core_editor : "";
        placeholder = "vim";
        break;
      case 4: // pull.rebase
        key = "pull.rebase";
        current_value = cfg.pull_rebase ? *cfg.pull_rebase : "";
        placeholder = "false";
        break;
      case 5: // push.default
        key = "push.default";
        current_value = cfg.push_default ? *cfg.push_default : "";
        placeholder = "simple";
        break;
      case 6: // color.ui
        key = "color.ui";
        current_value = cfg.color_ui.has_value() ? (*cfg.color_ui ? "true" : "false") : "true";
        placeholder = "true";
        break;
    }

    // Prompt for new value
    prompt::Prompt input_prompt(ectx.log);
    input_prompt.title("git-config")
                .style(flags.style)
                .question("Enter new value for " + key);

    if (!current_value.empty()) {
      input_prompt.fact("Current value", current_value);
    }

    std::string new_value = input_prompt.input(placeholder);

    if (new_value.empty() && current_value.empty()) {
      std::cout << "Skipped (no value entered)\n\n";
      continue;
    }

    if (new_value.empty()) {
      new_value = current_value;  // Keep current value
    }

    // Confirm change
    prompt::Prompt confirm_prompt(ectx.log);
    confirm_prompt.title("git-config")
                  .style(flags.style)
                  .force_yes(flags.force_yes)
                  .force_no(flags.force_no)
                  .question("Set " + key + " = \"" + new_value + "\"?");

    confirm_prompt.fact("Scope", scope_name)
                  .fact("Key", key)
                  .fact("New value", new_value);

    if (!current_value.empty() && current_value != new_value) {
      confirm_prompt.fact("Old value", current_value);
    }

    bool confirmed = confirm_prompt.confirm(true);

    if (!confirmed) {
      std::cout << "Skipped.\n\n";
      continue;
    }

    // Apply the change
    if (client.set_config_value(key, new_value, global)) {
      if (ectx.log) {
        ectx.log->info("Git::config", "Set " + key + "=" + new_value + " (" + scope_name + ")");
      }
      std::cout << "✓ Set " << key << " = \"" << new_value << "\"\n\n";

      // Refresh config
      cfg = client.get_full_config(global);
    } else {
      if (ectx.log) {
        ectx.log->error("Git::config", "Failed to set " + key);
      }
      std::cerr << "✗ Failed to set " << key << "\n\n";
    }
  }

  if (ectx.log) {
    ectx.log->info("Git::config", "Configuration session complete");
  }

  std::cout << "Configuration complete.\n";
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Hierarchical Command Routing (similar to bot subsystem)
// ═══════════════════════════════════════════════════════════════════════════

using CommandFn = int (*)(const directive::command_context&, const directive::context&);

// Helper to forward commands with adjusted argv
static int forward_git_command(const directive::command_context &ctx,
                               const directive::context &ectx,
                               CommandFn fn,
                               const std::string &command_name,
                               int arg_start,
                               std::initializer_list<std::string> extra = {}) {
  std::vector<std::string> storage;
  storage.reserve(2 + extra.size() + (ctx.argc > arg_start ? ctx.argc - arg_start : 0));

  if (ctx.argc > 0) {
    storage.emplace_back(ctx.argv[0]);
  } else {
    storage.emplace_back("nazg");
  }
  storage.emplace_back(command_name);

  for (const auto &item : extra) {
    storage.emplace_back(item);
  }

  for (int i = arg_start; i < ctx.argc; ++i) {
    storage.emplace_back(ctx.argv[i]);
  }

  std::vector<const char*> new_argv;
  new_argv.reserve(storage.size());
  for (const auto &s : storage) {
    new_argv.push_back(s.c_str());
  }

  directive::command_context new_ctx;
  new_ctx.argc = static_cast<int>(new_argv.size());
  new_ctx.argv = new_argv.data();

  return fn(new_ctx, ectx);
}

static void print_git_help(const std::string &prog) {
  std::cout << "Usage: " << prog << " git <subcommand> [options]\n\n";
  std::cout << "Git Repository Management\n\n";
  std::cout << "Subcommands:\n";
  std::cout << "  init              Initialize git repository\n";
  std::cout << "  status            Show git repository status\n";
  std::cout << "  add               Stage files for commit\n";
  std::cout << "  commit            Create a commit\n";
  std::cout << "  push              Push commits to remote\n";
  std::cout << "  clone             Clone a repository\n";
  std::cout << "  remote            Manage remotes\n";
  std::cout << "  checkout          Switch branches\n";
  std::cout << "  sync              Push to all remotes\n";
  std::cout << "  config            Interactive git configuration\n";
  std::cout << "  create-bare       Create local bare repository\n";
  std::cout << "  server            Manage git servers (cgit, gitea)\n";
  std::cout << "\n";
  std::cout << "Use '" << prog << " git <subcommand> --help' for more info\n";
}

static void print_git_server_help(const std::string &prog) {
  std::cout << "Usage: " << prog << " git server <subcommand> [options]\n\n";
  std::cout << "Git Server Management\n\n";
  std::cout << "Subcommands:\n";
  std::cout << "  add               Register a git server (cgit, gitea, gitlab)\n";
  std::cout << "  status            Show git server status\n";
  std::cout << "  install           Install git server software remotely\n";
  std::cout << "  sync              Sync local bare repos to server\n";
  std::cout << "\n";
  std::cout << "Use '" << prog << " git server <subcommand> --help' for more info\n";
}

// Router for: nazg git server <subcommand>
static int cmd_git_server_root(const directive::command_context& ctx,
                                const directive::context& ectx) {
  if (ctx.argc < 4) {
    print_git_server_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
    return 0;
  }

  std::string sub = to_lower_copy(ctx.argv[3]);
  if (sub == "--help" || sub == "-h" || sub == "help") {
    print_git_server_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
    return 0;
  }

  if (sub == "add") {
    return forward_git_command(ctx, ectx, cmd_git_server_add, "git-server-add", 4);
  }
  if (sub == "status") {
    return forward_git_command(ctx, ectx, cmd_git_server_status, "git-server-status", 4);
  }
  if (sub == "install") {
    return forward_git_command(ctx, ectx, cmd_git_server_install, "git-server-install", 4);
  }
  if (sub == "sync") {
    return forward_git_command(ctx, ectx, cmd_git_server_sync, "git-server-sync", 4);
  }

  std::cerr << "Unknown git server subcommand: " << sub << "\n\n";
  print_git_server_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
  return 2;
}

// Router for: nazg git <subcommand>
static int cmd_git_root(const directive::command_context& ctx,
                        const directive::context& ectx) {
  if (ctx.argc < 3) {
    print_git_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
    return 0;
  }

  std::string sub = to_lower_copy(ctx.argv[2]);
  if (sub == "--help" || sub == "-h" || sub == "help") {
    print_git_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
    return 0;
  }

  if (sub == "init") {
    return forward_git_command(ctx, ectx, cmd_git_init, "git-init", 3);
  }
  if (sub == "status") {
    return forward_git_command(ctx, ectx, cmd_git_status, "git-status", 3);
  }
  if (sub == "add") {
    return forward_git_command(ctx, ectx, cmd_git_add, "git-add", 3);
  }
  if (sub == "commit") {
    return forward_git_command(ctx, ectx, cmd_git_commit, "git-commit", 3);
  }
  if (sub == "push") {
    return forward_git_command(ctx, ectx, cmd_git_push, "git-push", 3);
  }
  if (sub == "clone") {
    return forward_git_command(ctx, ectx, cmd_git_clone, "git-clone", 3);
  }
  if (sub == "remote") {
    return forward_git_command(ctx, ectx, cmd_git_remote, "git-remote", 3);
  }
  if (sub == "checkout") {
    return forward_git_command(ctx, ectx, cmd_git_checkout, "git-checkout", 3);
  }
  if (sub == "sync") {
    return forward_git_command(ctx, ectx, cmd_git_sync, "git-sync", 3);
  }
  if (sub == "config") {
    return forward_git_command(ctx, ectx, cmd_git_config, "git-config", 3);
  }
  if (sub == "create-bare") {
    return forward_git_command(ctx, ectx, cmd_git_create_bare, "git-create-bare", 3);
  }
  if (sub == "server") {
    return cmd_git_server_root(ctx, ectx);
  }

  std::cerr << "Unknown git subcommand: " << sub << "\n\n";
  print_git_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
  return 2;
}

void register_commands(directive::registry &reg, const directive::context &/*ctx*/) {
  // Hierarchical git command (new style: "nazg git <subcommand>")
  reg.add("git", "Git repository and server management", cmd_git_root);

  // Keep old-style commands for backward compatibility (can be removed later)
  reg.add("git-init", "Initialize git repository (deprecated: use 'git init')", cmd_git_init);
  reg.add("git-status", "Show git status (deprecated: use 'git status')", cmd_git_status);
  reg.add("git-create-bare", "Create local bare repository (deprecated: use 'git create-bare')", cmd_git_create_bare);
  reg.add("git-commit", "Create a commit (deprecated: use 'git commit')", cmd_git_commit);
  reg.add("git-sync", "Push to all remotes (deprecated: use 'git sync')", cmd_git_sync);
  reg.add("git-config", "Interactive git configuration (deprecated: use 'git config')", cmd_git_config);
  reg.add("git-server-add", "Register a git server (deprecated: use 'git server add')", cmd_git_server_add);
  reg.add("git-server-status", "Show git server status (deprecated: use 'git server status')", cmd_git_server_status);
  reg.add("git-server-install", "Install git server software (deprecated: use 'git server install')", cmd_git_server_install);
  reg.add("git-server-sync", "Sync local bare repos to server (deprecated: use 'git server sync')", cmd_git_server_sync);
}

} // namespace nazg::git

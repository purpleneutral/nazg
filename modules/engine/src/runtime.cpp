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

#include "engine/runtime.hpp"
#include "engine/updater.hpp"
#include "engine/cmd_info.hpp"

#include "brain/commands.hpp"
#include "brain/detector.hpp"
#include "test/commands.hpp"
#include "git/commands.hpp"
#include "git/client.hpp"
#include "scaffold/commands.hpp"
#include "bot/commands.hpp"
#include "docker_monitor/commands.hpp"
#include "workspace/commands.hpp"
#include "tui/commands.hpp"
#include "tui/tui.hpp"
#include "prompt/demo.hpp"
#include "prompt/prompt.hpp"

#include "blackbox/logger.hpp"
#include "blackbox/options.hpp"

#include "config/config.hpp"

#include "directive/builtins.hpp"
#include "directive/context.hpp"
#include "directive/db_commands.hpp"
#include "directive/agent_commands.hpp"
#include "directive/registry.hpp"

#include "nexus/config.hpp"
#include "nexus/store.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <unistd.h>

namespace fs = std::filesystem;

namespace nazg::engine {

struct runtime::impl {
  options opts;

  std::unique_ptr<config::store> cfg;
  std::unique_ptr<blackbox::logger> log;
  std::unique_ptr<nexus::Store> db;

  std::unique_ptr<directive::registry> reg;
  std::unique_ptr<directive::context> dctx;

  explicit impl(const options &o) : opts(o) {}
};

runtime::runtime(const options &opts) : p_(std::make_unique<impl>(opts)) {
  p_->cfg = std::make_unique<config::store>();
}
runtime::~runtime() = default;

// ----- Logging (Blackbox) ---------------------------------------------------

void runtime::init_logging() {
  // Start with config overrides (without logger yet)
  auto bool_to_text = [](bool value) { return value ? "true" : "false"; };

  // Override options from config if present
  if (p_->cfg->has("blackbox", "min_level")) {
    std::string level_str = p_->cfg->get_string("blackbox", "min_level");
    p_->opts.log.min_level = blackbox::parse_level(level_str);
  }

  if (p_->cfg->has("blackbox", "console_enabled"))
    p_->opts.log.console_enabled =
        p_->cfg->get_bool("blackbox", "console_enabled");

  if (p_->cfg->has("blackbox", "console_colors"))
    p_->opts.log.console_colors =
        p_->cfg->get_bool("blackbox", "console_colors");

  if (p_->cfg->has("blackbox", "color_in_file"))
    p_->opts.log.color_in_file =
        p_->cfg->get_bool("blackbox", "color_in_file");

  if (p_->cfg->has("blackbox", "file_path"))
    p_->opts.log.file_path = p_->cfg->get_string("blackbox", "file_path");

  if (p_->cfg->has("blackbox", "rotate_bytes"))
    p_->opts.log.rotate_bytes = p_->cfg->get_int("blackbox", "rotate_bytes");

  if (p_->cfg->has("blackbox", "rotate_files"))
    p_->opts.log.rotate_files = p_->cfg->get_int("blackbox", "rotate_files");

  if (p_->cfg->has("blackbox", "source_style")) {
    std::string style_str = p_->cfg->get_string("blackbox", "source_style");
    p_->opts.log.src_style = blackbox::parse_source_style(style_str);
  }

  if (p_->cfg->has("blackbox", "include_ms"))
    p_->opts.log.include_ms = p_->cfg->get_bool("blackbox", "include_ms");

  if (p_->cfg->has("blackbox", "pad_level"))
    p_->opts.log.pad_level = p_->cfg->get_bool("blackbox", "pad_level");

  if (p_->cfg->has("blackbox", "tag_width"))
    p_->opts.log.tag_width = p_->cfg->get_int("blackbox", "tag_width");

  if (p_->cfg->has("blackbox", "include_tid"))
    p_->opts.log.include_tid = p_->cfg->get_bool("blackbox", "include_tid");

  if (p_->cfg->has("blackbox", "include_pid"))
    p_->opts.log.include_pid = p_->cfg->get_bool("blackbox", "include_pid");

  // Then CLI args can still override config
  p_->log = std::make_unique<blackbox::logger>(p_->opts.log);

  p_->log->info("Engine", "Logger initialized");
  p_->log->debug(
      "Engine",
      "log.min_level=" + std::to_string(static_cast<int>(p_->opts.log.min_level)) +
          " console_enabled=" + bool_to_text(p_->opts.log.console_enabled) +
          " console_colors=" + bool_to_text(p_->opts.log.console_colors) +
          " color_in_file=" + bool_to_text(p_->opts.log.color_in_file));
  if (!p_->opts.log.file_path.empty()) {
    p_->log->debug("Engine", "log.file_path=" + p_->opts.log.file_path);
  }

  // Policy: silent console by default (log to file only)
  // Let env override (NAZG_LOG_CONSOLE=1/true/TRUE)
  if (const char *e = std::getenv("NAZG_LOG_CONSOLE")) {
    std::string v = e;
    if (v == "1" || v == "true" || v == "TRUE") {
      p_->log->set_console_enabled(true);
      p_->log->info("Engine", "Console logging enabled via NAZG_LOG_CONSOLE");
    }
  }

  // CLI --verbose wins
  if (p_->opts.verbose) {
    p_->log->set_console_enabled(true);
    p_->log->info("Engine", "Console logging enabled via --verbose");
  }

  // Now that logger is initialized and console policy is set, give it to config
  p_->cfg->set_logger(p_->log.get());

  // Reload config so it logs the load operation (will respect console setting)
  p_->cfg->reload();
  p_->log->info("Engine", "Configuration reloaded with logging attached");
}

::nazg::blackbox::logger *runtime::logger() const { return p_->log.get(); }

const config::store *runtime::config() const { return p_->cfg.get(); }

// ----- Nexus (Database) ----------------------------------------------------

void runtime::init_nexus() {
  if (!p_->cfg) {
    if (p_->log)
      p_->log->error("Engine", "Cannot init nexus: config not loaded");
    return;
  }

  if (p_->log) {
    p_->log->info("Nexus", "Initializing persistence layer");
  }

  // Load nexus config
  auto nexus_cfg = nexus::Config::from_config(*p_->cfg);

  if (p_->log)
    p_->log->info("Nexus", "Initializing database at: " + nexus_cfg.db_path);

  // Create store
  p_->db = nexus::Store::create(nexus_cfg.db_path, p_->log.get());
  if (!p_->db) {
    if (p_->log)
      p_->log->error("Nexus", "Failed to create database");
    std::cerr << "Error: Failed to create database at " << nexus_cfg.db_path << "\n";
    std::cerr << "Check file permissions and disk space.\n";
    return;
  }

  // Initialize (run migrations)
  if (!p_->db->initialize()) {
    std::string error = p_->db->last_init_error();
    if (p_->log)
      p_->log->error("Nexus", "Failed to initialize database: " + error);

    std::cerr << "Error: Database initialization failed at " << nexus_cfg.db_path << "\n";
    std::cerr << "Reason: " << error << "\n\n";

    // Intelligent recovery: detect corruption and offer auto-reset
    bool looks_corrupted = (error.find("corrupt") != std::string::npos ||
                           error.find("malformed") != std::string::npos ||
                           error.find("database disk image is malformed") != std::string::npos);

    if (looks_corrupted) {
      std::cerr << "The database appears to be corrupted.\n";
      std::cerr << "Auto-recovery: Removing corrupt database and recreating...\n";

      // Close the database first
      p_->db.reset();

      // Remove corrupt database
      try {
        std::filesystem::remove(nexus_cfg.db_path);
        std::cerr << "Removed corrupt database.\n";
        if (p_->log)
          p_->log->warn("Nexus", "Removed corrupt database file: " + nexus_cfg.db_path);
      } catch (const std::exception& e) {
        std::cerr << "Failed to remove corrupt database: " << e.what() << "\n";
        if (p_->log)
          p_->log->error("Nexus",
                         "Failed to remove corrupt database: " + std::string(e.what()));
        return;
      }

      // Recreate database
      p_->db = nexus::Store::create(nexus_cfg.db_path, p_->log.get());
      if (!p_->db) {
        std::cerr << "Failed to recreate database after corruption recovery.\n";
        if (p_->log)
          p_->log->error("Nexus", "Failed to recreate database after removal");
        return;
      }

      // Try initialization again
      if (!p_->db->initialize()) {
        std::cerr << "Failed to initialize new database: " << p_->db->last_init_error() << "\n";
        if (p_->log)
          p_->log->error("Nexus",
                         "Failed to initialize new database after recovery: " +
                             p_->db->last_init_error());
        p_->db.reset();
        return;
      }

      std::cerr << "✓ Database recovered successfully.\n";
      if (p_->log)
        p_->log->info("Nexus", "Database recovery succeeded");
    } else {
      std::cerr << "To reset the database manually, run: rm " << nexus_cfg.db_path << "\n";
      if (p_->log)
        p_->log->warn("Nexus", "Database initialization failed: " + error);
      p_->db.reset();
      return;
    }
  }

  if (p_->log)
    p_->log->info("Nexus", "Database initialized successfully");
}

nexus::Store *runtime::nexus() const { return p_->db.get(); }

// ---------------- Update Command -------------------------------------------

namespace {
int cmd_update(const directive::command_context &cctx,
               const directive::context &ectx) {
  // Parse flags
  update::Config cfg = update::default_config();
  cfg.log = ectx.log;
  cfg.verbose = ectx.verbose;
  bool do_rollback = false;
  std::optional<std::string> rollback_version;

  for (int i = 2; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout
          << "Usage: nazg update [options]\n"
          << "Options:\n"
          << "  --rollback [VERSION]  Roll back to previous or specific "
             "version\n"
          << "  --ref REF             Git ref to build (tag/branch/commit)\n"
          << "  --src PATH            Use specific source directory\n"
          << "  --prefix PATH         Install prefix (default: "
             "$XDG_DATA_HOME/nazg/versions)\n"
          << "  --bin-dir PATH        Binary directory (default: "
             "~/.local/bin)\n"
          << "  --keep N              Keep N most recent versions (default: "
             "3)\n"
          << "  --dry-run             Build but don't activate\n"
          << "  --reexec              Re-exec into new binary on success\n"
          << "  --no-local            Force git clone/fetch (don't use local "
             "source)\n"
          << "  -h, --help            Show this help\n";
      return 0;
    } else if (arg == "--rollback") {
      do_rollback = true;
      if (i + 1 < cctx.argc && cctx.argv[i + 1][0] != '-') {
        rollback_version = cctx.argv[++i];
      }
    } else if (arg == "--ref" && i + 1 < cctx.argc) {
      cfg.ref = cctx.argv[++i];
    } else if (arg == "--src" && i + 1 < cctx.argc) {
      cfg.local_src_hint = cctx.argv[++i];
    } else if (arg == "--prefix" && i + 1 < cctx.argc) {
      cfg.prefix = cctx.argv[++i];
    } else if (arg == "--bin-dir" && i + 1 < cctx.argc) {
      cfg.bin_dir = cctx.argv[++i];
    } else if (arg == "--keep" && i + 1 < cctx.argc) {
      cfg.keep = std::atoi(cctx.argv[++i]);
    } else if (arg == "--dry-run") {
      cfg.dry_run = true;
    } else if (arg == "--reexec") {
      cfg.reexec_after = true;
    } else if (arg == "--no-local") {
      cfg.prefer_local = false;
    } else {
      if (ectx.log) {
        ectx.log->error("Engine", "Unknown update option: " + std::string(arg));
      }
      std::cerr << "Unknown option: " << arg << "\n";
      return 1;
    }
  }

  update::UpdateResult result;

  if (do_rollback) {
    if (ectx.verbose) {
      std::cout << "Rolling back";
      if (rollback_version) {
        std::cout << " to version " << *rollback_version;
      }
      std::cout << "...\n";
    }
    result = update::rollback(cfg, rollback_version);
  } else {
    if (ectx.verbose) {
      std::cout << "Updating from source...\n";
      if (!cfg.ref.empty()) {
        std::cout << "  Target ref: " << cfg.ref << "\n";
      }
      if (!cfg.local_src_hint.empty()) {
        std::cout << "  Source hint: " << cfg.local_src_hint << "\n";
      }
    }
    result = update::update_from_source(cfg);
  }

  if (result.ok) {
    if (ectx.log) {
      ectx.log->info("Engine", "Update successful: " + result.message);
    }
    std::cout << "✓ " << result.message << "\n";
    if (!result.activated_version.empty()) {
      std::cout << "  Active version: " << result.activated_version << "\n";
    }
    return 0;
  } else {
    if (ectx.log) {
      ectx.log->error("Engine", "Update failed: " + result.message);
    }
    std::cerr << "✗ Update failed: " << result.message << "\n";
    return 1;
  }
}
} // namespace

namespace {
int cmd_status(const directive::command_context &cctx,
               const directive::context &ectx) {
  (void)cctx;

  std::string cwd;
  char buf[4096];
  if (::getcwd(buf, sizeof(buf))) {
    cwd = buf;
  } else {
    cwd = ".";
  }

  std::cout << "📦 Nazg Project Status\n\n";
  std::cout << "Directory : " << cwd << "\n";

  brain::Detector detector(ectx.store, ectx.log);
  auto info = detector.detect(cwd);

  std::cout << "Language  : "
            << (info.language.empty() ? "unknown" : info.language) << "\n";
  std::cout << "Build sys : "
            << (info.build_system.empty() ? "unknown" : info.build_system)
            << "\n";
  std::cout << "SCM       : " << info.scm << "\n";

  if (!info.tools.empty()) {
    std::cout << "Tools     : ";
    for (size_t i = 0; i < info.tools.size(); ++i) {
      if (i)
        std::cout << ", ";
      std::cout << info.tools[i];
    }
    std::cout << "\n";
  }

  std::cout << "\n";

  git::Client git_client(cwd, ectx.log);
  if (!git_client.is_repo()) {
    std::cout << "Git       : not a repository\n";
  } else {
    auto status = git_client.status();
    std::cout << "Git branch: " << status.branch << "\n";
    if (status.upstream) {
      std::cout << "Upstream  : " << *status.upstream << "\n";
    }
    if (status.ahead > 0 || status.behind > 0) {
      std::cout << "Divergence: " << status.ahead << " ahead, "
                << status.behind << " behind\n";
    }
    std::cout << "Changes   : " << status.modified << " modified, "
              << status.staged << " staged, " << status.untracked
              << " untracked\n";
  }

  std::cout << "\nTip: Run 'nazg' for interactive assistance." << std::endl;

  return 0;
}
} // namespace

// ---------------- Directive (commands) -------------------------------------
void runtime::init_commands() {
  p_->reg = std::make_unique<directive::registry>();
  p_->dctx = std::make_unique<directive::context>();

  if (p_->log) {
    p_->log->info("Engine", "Registering directive commands");
  }

  // Minimal context
  p_->dctx->log = p_->log.get();
  p_->dctx->store = p_->db.get();  // Pass nexus store to commands
  p_->dctx->cfg = p_->cfg.get();   // Pass config store to commands
  p_->dctx->verbose = p_->opts.verbose;
  p_->dctx->reg = p_->reg.get();

  // Register built-in directive meta-command
  directive::register_info(*p_->reg);

  // Register update command
  p_->reg->add("update", "Update or rollback nazg binary from source",
               cmd_update);

  // Register project status command
  p_->reg->add("status", "Show project summary for current directory",
               cmd_status);

  // Register system info command (stays in engine)
  register_info_command(*p_->reg);

  // Modules self-register their commands
  brain::register_commands(*p_->reg, *p_->dctx);
  test::register_commands(*p_->reg, *p_->dctx);
  scaffold::register_commands(*p_->reg, *p_->dctx);
  git::register_commands(*p_->reg, *p_->dctx);
  bot::register_commands(*p_->reg, *p_->dctx);
  docker_monitor::register_commands(*p_->reg, *p_->dctx);
  directive::register_db_commands(*p_->reg, *p_->dctx);
  directive::register_agent_commands(*p_->reg, *p_->dctx);
  workspace::register_commands(*p_->reg, *p_->dctx);
  tui::register_commands(*p_->reg, *p_->dctx);
  prompt::register_demo_command(*p_->reg);

  if (p_->log) {
    std::size_t count = p_->reg->all().size();
    p_->log->info("Engine", "Registered " + std::to_string(count) + " commands");
  }
}

directive::registry &runtime::registry() { return *p_->reg; }

// ── Intelligent Assistant Mode ──
namespace {
int run_assistant_mode(const directive::context &ectx,
                       const directive::registry &reg) {
  auto *log = ectx.log;

  if (log) {
    log->info("Assistant", "Starting intelligent assistant mode");
  }

  // Detect current directory context
  std::string cwd;
  char buf[4096];
  if (::getcwd(buf, sizeof(buf))) {
    cwd = buf;
  } else {
    cwd = ".";
  }

  if (log) {
    log->debug("Assistant", "Current directory: " + cwd);
  }

  // Use brain detector to understand environment
  brain::Detector detector(ectx.store, log);
  auto project_info = detector.detect(cwd);

  if (log) {
    log->debug("Assistant", "Detected language: " + project_info.language);
    log->debug("Assistant", "Detected build system: " + project_info.build_system);
    log->debug("Assistant", "Has git: " + std::string(project_info.has_git ? "yes" : "no"));
  }

  // ── Build context-aware prompt ──
  prompt::Prompt assistant(log);
  assistant.title("nazg assistant")
           .question("👋 Hi! I'm Nazg. How can I help?");

  // Show detected context
  fs::path cwd_path(cwd);
  std::string dir_name = cwd_path.filename().string();

  assistant.fact("Current directory", dir_name)
           .fact("Full path", cwd);

  bool is_empty = true;
  int file_count = 0;
  if (fs::exists(cwd)) {
    for (const auto &entry : fs::directory_iterator(cwd)) {
      std::string filename = entry.path().filename().string();
      if (filename[0] != '.') {
        is_empty = false;
      }
      file_count++;
    }
  }

  if (project_info.language != "unknown") {
    assistant.fact("Language detected", project_info.language);
  }

  if (project_info.has_git) {
    assistant.fact("Git repository", "yes");

    // Get git status
    git::Client client(cwd, log);
    auto status = client.status();
    if (status.in_repo) {
      assistant.fact("Git branch", status.branch);
      if (status.upstream) {
        assistant.fact("Upstream", *status.upstream);
      }
      if (status.ahead > 0 || status.behind > 0) {
        assistant.fact("Divergence", std::to_string(status.ahead) +
                                      " ahead / " + std::to_string(status.behind) +
                                      " behind");
      }
      if (status.modified > 0 || status.staged > 0 || status.untracked > 0) {
        std::string changes = std::to_string(status.modified) + " modified, " +
                             std::to_string(status.staged) + " staged, " +
                             std::to_string(status.untracked) + " untracked";
        assistant.fact("Changes", changes);
      }
    }
  }

  if (project_info.has_cmake) {
    assistant.fact("Build system", "CMake");
  } else if (project_info.has_makefile) {
    assistant.fact("Build system", "Make");
  }

  // ── Build contextual action menu ──
  std::vector<std::string> actions;
  std::vector<std::string> action_commands;

  // Always available: view help
  actions.push_back("Show all available commands");
  action_commands.push_back("info");

  actions.push_back("View project status summary");
  action_commands.push_back("status");

  // Context-specific actions
  if (is_empty || file_count <= 2) {
    // Empty or nearly empty directory - suggest project init
    assistant.info("This directory looks empty. Would you like to start a new project?");
    actions.push_back("🚀 Create a new project (C++/Python/C)");
    action_commands.push_back("init");
  }

  if (!project_info.has_git && !is_empty) {
    // Has files but no git - suggest git init
    assistant.info("I notice you don't have version control set up.");
    actions.push_back("Initialize git repository");
    action_commands.push_back("git-init");
  }

  if (project_info.has_git) {
    git::Client client(cwd, log);
    auto status = client.status();

    if (status.modified > 0 || status.untracked > 0) {
      actions.push_back("Commit changes");
      action_commands.push_back("git-commit");
    }

    actions.push_back("View git status");
    action_commands.push_back("git-status");

    actions.push_back("Configure git settings");
    action_commands.push_back("git-config");
  }

  if (project_info.has_cmake || (project_info.language == "cpp" && !is_empty)) {
    actions.push_back("Build project");
    action_commands.push_back("build");

    actions.push_back("Run tests");
    action_commands.push_back("test");
  }

  if (project_info.language == "python" && !is_empty) {
    actions.push_back("Detect Python project");
    action_commands.push_back("detect");
  }

  // Always available
  actions.push_back("Update nazg");
  action_commands.push_back("update");

  actions.push_back("Exit");
  action_commands.push_back("exit");

  // Show the menu
  int choice = assistant.choice(actions, static_cast<int>(actions.size()) - 1);

  if (choice < 0 || choice >= static_cast<int>(actions.size())) {
    std::cout << "Goodbye!\n";
    return 0;
  }

  std::string selected_command = action_commands[choice];

  if (selected_command == "exit") {
    std::cout << "Goodbye!\n";
    return 0;
  }

  if (log) {
    log->info("Assistant", "User selected: " + selected_command);
  }

  // Dispatch the selected command through the registry
  std::string prog_str = ectx.prog;
  std::vector<const char *> synth_argv;
  synth_argv.push_back(prog_str.c_str());
  synth_argv.push_back(selected_command.c_str());

  std::cout << "\n";
  auto [found, code] = reg.dispatch(selected_command, ectx, synth_argv);
  if (!found) {
    std::cerr << "Command not found: " << selected_command << "\n";
    return 2;
  }
  return code;
}
} // namespace

int runtime::dispatch(int argc, char **argv) {
  if (!p_->reg || !p_->dctx) {
    if (p_->dctx && p_->dctx->log) {
      p_->dctx->log->error("Engine", "Directive not initialized");
    }
    std::cerr << "Directive not initialized; call init_commands()\n";
    return 2;
  }

  // Populate context with current invocation
  p_->dctx->argc = argc;
  p_->dctx->argv = argv;
  if (argc > 0)
    p_->dctx->prog = argv[0];

  std::vector<const char *> av;
  av.reserve(argc);
  for (int i = 0; i < argc; ++i)
    av.push_back(argv[i]);

  std::string cmd = (argc >= 2) ? argv[1] : "";

  if (p_->dctx->log) {
    std::ostringstream os;
    os << "argv=";
    for (int i = 0; i < argc; ++i) {
      if (i) os << ' ';
      os << argv[i];
    }
    p_->dctx->log->debug("Engine", "dispatch invoked with " + os.str());
  }

  // Check for global --help flag
  if (cmd == "--help" || cmd == "-h") {
    p_->reg->print_help(argv[0]);
    std::cout << "\nGlobal options:\n";
    std::cout << "  --help, -h    Show this help message\n";
    std::cout << "\nRun 'nazg' with no arguments for interactive assistant mode.\n";
    std::cout << "Run 'nazg <command> --help' for help on a specific command.\n";
    return 0;
  }

  // No command given - launch intelligent assistant
  if (cmd.empty()) {
    // Non-interactive terminals get help instead of blocking on stdin
    if (!::isatty(STDIN_FILENO)) {
      if (p_->dctx->log)
        p_->dctx->log->info("Engine", "Non-interactive terminal, showing help");
      p_->reg->print_help(argv[0]);
      return 0;
    }
    if (p_->dctx->log)
      p_->dctx->log->info("Engine", "No command given, launching assistant mode");
    return run_assistant_mode(*p_->dctx, *p_->reg);
  }

  auto [found, code] = p_->reg->dispatch(cmd, *p_->dctx, av);
  if (p_->dctx->log && found) {
    p_->dctx->log->info("Engine", "Command '" + cmd + "' executed with code " + std::to_string(code));
  }
  if (!found) {
    if (p_->dctx->log) {
      p_->dctx->log->warn("Engine", "Unknown command: " + cmd);
    }
    std::cerr << "Unknown command: " << cmd << "\n\n";
    p_->reg->print_help(argv[0]);
    std::cout << "\n💡 Tip: Run 'nazg' with no arguments for interactive assistant mode.\n";
    return 2;
  }
  return code;
}

} // namespace nazg::engine

#include "test/commands.hpp"
#include "test/runner.hpp"
#include "test/types.hpp"
#include "test/workspace_suite.hpp"

#include "brain/detector.hpp"
#include "brain/planner.hpp"
#include "blackbox/options.hpp"
#include "blackbox/logger.hpp"
#include "config/config.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include "nexus/store.hpp"
#include "prompt/prompt.hpp"
#include "prompt/colors.hpp"
#include "prompt/gradient.hpp"
#include "system/process.hpp"
#include "system/terminal.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string_view>
#include <vector>
#include <unistd.h>

namespace {

std::string get_cwd() {
  char buf[4096];
  if (::getcwd(buf, sizeof(buf))) {
    return buf;
  }
  return ".";
}

enum class MergeTestMode {
  LOCAL,
  REMOTE
};

struct MergeScenarioOptions {
  MergeTestMode mode = MergeTestMode::LOCAL;
  std::string label = "";
  std::string host = "";
  std::string setup_user = "";
  std::string git_user = "";
  std::string repo_base = "";
  std::string project_lang = "cpp";
  bool cleanup = true;  // Always cleanup by default
  bool auto_mode = false;  // Interactive by default
  bool host_overridden = false;
  bool repo_base_overridden = false;
  bool setup_user_overridden = false;
  bool git_user_overridden = false;
  bool git_shell_mode = false;
};

struct StepResult {
  std::string name;
  std::string command;
  std::string output;
  int exit_code = -1;
  bool expected_success = true;
  bool success = false;
};

std::string random_suffix() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 15);
  std::ostringstream oss;
  for (int i = 0; i < 8; ++i) {
    oss << std::hex << dist(gen);
  }
  return oss.str();
}

// Filter git hints and warnings from output
std::string filter_git_output(const std::string& output) {
  std::istringstream iss(output);
  std::ostringstream oss;
  std::string line;
  bool in_hint_block = false;

  while (std::getline(iss, line)) {
    // Skip hint lines
    if (line.find("hint:") != std::string::npos) {
      in_hint_block = true;
      continue;
    }
    // Skip warning lines about remote HEAD
    if (line.find("warning: remote HEAD refers to nonexistent ref") != std::string::npos) {
      continue;
    }
    // Keep other lines
    if (!line.empty() || !in_hint_block) {
      oss << line << "\n";
      in_hint_block = false;
    }
  }

  std::string result = oss.str();
  // Remove trailing newlines
  while (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }
  return result;
}

// Create a centered box with dynamic width
std::string make_box_top(int width) {
  return "┌" + std::string(width - 2, '─') + "┐";
}

std::string make_box_bottom(int width) {
  return "└" + std::string(width - 2, '─') + "┘";
}

std::string make_box_line(const std::string& text, int width) {
  int padding = width - text.length() - 4; // 4 for "│  " and "  │"
  int left_pad = padding / 2;
  int right_pad = padding - left_pad;
  return "│ " + std::string(left_pad, ' ') + text + std::string(right_pad, ' ') + " │";
}

std::string make_info_line(const std::string& label, const std::string& value, int width) {
  std::string line = label + value;
  int padding = width - line.length() - 4;
  if (padding < 0) padding = 0;
  return "│ " + line + std::string(padding, ' ') + " │";
}

void wait_for_user(const MergeScenarioOptions &opts, const std::string &message = "") {
  if (opts.auto_mode) {
    return;
  }

  nazg::prompt::ColorFormatter fmt(true);

  if (!message.empty()) {
    std::cout << "\n" << fmt.bold(message) << "\n";
  }
  std::cout << fmt.dim("  Press Enter to continue... ");
  std::cout.flush();
  std::string dummy;
  std::getline(std::cin, dummy);
}

StepResult run_shell_step(const std::string &name, const std::string &cmd,
                          const std::filesystem::path &cwd,
                          bool expected_success = true) {
  StepResult result;
  result.name = name;
  std::ostringstream full_cmd;
  if (!cwd.empty()) {
    full_cmd << "cd " << nazg::system::shell_quote(cwd.string()) << " && ";
  }
  full_cmd << cmd;
  result.command = full_cmd.str();

  nazg::prompt::ColorFormatter fmt(true);
  int term_width = nazg::system::term_width();
  if (term_width < 60) term_width = 80;

  // Step header
  std::cout << "\n" << fmt.bold(fmt.cyan("┌─ " + name)) << "\n";
  std::cout << fmt.dim("│ $ " + cmd) << "\n";
  std::cout << fmt.cyan("└" + std::string(term_width - 2, '─')) << "\n";

  nazg::system::CommandResult proc = nazg::system::run_command_capture(result.command);

  result.exit_code = proc.exit_code;
  result.output = proc.output;
  result.expected_success = expected_success;
  result.success = (expected_success ? (proc.exit_code == 0) : (proc.exit_code != 0));

  // Filter and display output
  if (!proc.output.empty()) {
    std::string filtered = filter_git_output(proc.output);
    if (!filtered.empty()) {
      // Indent output
      std::istringstream iss(filtered);
      std::string line;
      while (std::getline(iss, line)) {
        std::cout << fmt.dim("  " + line) << "\n";
      }
    }
  }

  // Result indicator
  std::cout << "\n";
  if (result.success) {
    std::cout << fmt.green("  ✓ " + name + (expected_success ? "" : " (expected failure)")) << "\n";
  } else {
    std::cout << fmt.red("  ✗ " + name + " (exit code " + std::to_string(proc.exit_code) + ")") << "\n";
  }

  return result;
}

StepResult run_git_step(const std::string &name, const std::string &cmd,
                        const std::filesystem::path &cwd,
                        bool expected_success = true) {
  return run_shell_step(name, cmd, cwd, expected_success);
}

void load_defaults_from_config(MergeScenarioOptions &opts,
                               nazg::config::store *cfg,
                               nazg::blackbox::logger *log) {
  if (!cfg || opts.mode == MergeTestMode::LOCAL) {
    return;
  }

  if (opts.label.empty()) {
    if (log) {
      log->debug("TestRunner.merge", "No label specified, skipping config load");
    }
    return;
  }

  std::string section = "git.servers." + opts.label;

  if (!opts.host_overridden) {
    std::string cfg_host = cfg->get_string(section, "host", "");
    if (!cfg_host.empty()) {
      opts.host = cfg_host;
    }
  }

  if (!opts.setup_user_overridden) {
    std::string cfg_ssh_user = cfg->get_string(section, "ssh_user", "");
    if (!cfg_ssh_user.empty()) {
      opts.setup_user = cfg_ssh_user;
    }
  }

  if (!opts.repo_base_overridden) {
    std::string cfg_repo_path = cfg->get_string(section, "repo_base_path", "");
    if (!cfg_repo_path.empty()) {
      opts.repo_base = cfg_repo_path;
    }
  }

  if (!opts.git_user_overridden) {
    std::string cfg_git_user = cfg->get_string(section, "git_user", "");
    if (!cfg_git_user.empty()) {
      opts.git_user = cfg_git_user;
    }
  }

  if (log) {
    log->debug("TestRunner.merge", "Loaded defaults for label " + opts.label +
                                       ": host=" + opts.host +
                                       ", setup_user=" + opts.setup_user +
                                       ", git_user=" + opts.git_user +
                                       ", repo_base=" + opts.repo_base);
  }
}

bool check_admin_shell(const MergeScenarioOptions &opts, std::string &error_output) {
  std::ostringstream cmd;
  cmd << "ssh " << opts.setup_user << "@" << opts.host << " "
      << nazg::system::shell_quote("echo __nazg_admin_check__");
  auto res = nazg::system::run_command_capture(cmd.str());
  if (res.exit_code != 0) {
    error_output = res.output;
    return false;
  }
  return true;
}

bool ensure_remote_repository(const MergeScenarioOptions &opts,
                              const std::string &remote_repo_path) {
  std::string quoted_repo_base = nazg::system::shell_quote(opts.repo_base);
  std::string quoted_repo_path = nazg::system::shell_quote(remote_repo_path);
  std::ostringstream remote_cmd;
  remote_cmd << "set -e; "
             << "if ! sudo -n test -d " << quoted_repo_base << "; then "
             << "sudo -n mkdir -p " << quoted_repo_base << "; "
             << "fi; "
             << "if sudo -n test -e " << quoted_repo_path << "; then "
             << "exit 2; "
             << "fi; "
             << "sudo -n -u " << opts.git_user << " git init --bare "
             << quoted_repo_path << " >/dev/null";

  std::ostringstream ssh_cmd;
  ssh_cmd << "ssh " << opts.setup_user << "@" << opts.host << " "
          << nazg::system::shell_quote(remote_cmd.str());

  auto res = nazg::system::run_command_capture(ssh_cmd.str());
  if (res.exit_code == 2) {
    std::cerr << "✗ Remote repository already exists at " << remote_repo_path << "\n";
    return false;
  }
  if (res.exit_code != 0) {
    std::cerr << "✗ Failed to prepare remote repository using " << opts.setup_user
              << "@" << opts.host << ": " << res.output << "\n";
    std::cerr << "   Ensure the user has passwordless sudo rights or specify an alternate admin with --setup-user." << "\n";
    return false;
  }
  return true;
}

bool verify_remote_repository_git_shell(const std::string &remote_git_url,
                                        std::string &error_output) {
  std::ostringstream cmd;
  cmd << "git ls-remote " << nazg::system::shell_quote(remote_git_url) << " HEAD";
  auto res = nazg::system::run_command_capture(cmd.str());
  if (res.exit_code != 0) {
    error_output = res.output;
    return false;
  }
  return true;
}

int run_git_merge_collision_scenario(const nazg::directive::command_context &cctx,
                                     const nazg::directive::context &ectx,
                                     int arg_start) {
  MergeScenarioOptions options;

  // First, check for mode (local/remote)
  if (arg_start < cctx.argc) {
    std::string mode_arg = cctx.argv[arg_start];
    if (mode_arg == "local") {
      options.mode = MergeTestMode::LOCAL;
      arg_start++;
    } else if (mode_arg == "remote") {
      options.mode = MergeTestMode::REMOTE;
      arg_start++;
    } else if (mode_arg != "--help" && mode_arg != "-h" &&
               !mode_arg.empty() && mode_arg[0] != '-') {
      std::cerr << "Unknown mode: " << mode_arg << "\n";
      std::cerr << "Expected 'local' or 'remote'\n";
      std::cerr << "Usage: nazg test git merge collision [local|remote] [auto] [options]\n";
      return 1;
    }
  }

  // Check for "auto" flag immediately after mode
  if (arg_start < cctx.argc && std::string(cctx.argv[arg_start]) == "auto") {
    options.auto_mode = true;
    arg_start++;
  }

  for (int i = arg_start; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg test git merge collision [local|remote] [auto] [options]\n\n";
      std::cout << "Mode:\n";
      std::cout << "  local                  Use local bare repository (no SSH required)\n";
      std::cout << "  remote                 Use remote git server (requires SSH setup)\n\n";
      std::cout << "Execution:\n";
      std::cout << "  auto                   Run without interactive prompts (default: interactive)\n\n";
      std::cout << "Options:\n";
      std::cout << "  --project-lang LANG    Project language for nazg init (c|cpp|python, default: cpp)\n";
      std::cout << "  --no-cleanup           Keep temporary workspace after completion\n\n";
      std::cout << "Remote mode options:\n";
      std::cout << "  --label NAME           Server label in Nazg config (git.servers.<label>)\n";
      std::cout << "  --host HOST            Remote git host (required if not in config)\n";
      std::cout << "  --setup-user USER      SSH user with sudo rights (required if not in config)\n";
      std::cout << "  --git-user USER        Git SSH user (default: git)\n";
      std::cout << "  --repo-base PATH       Remote repo base path (required if not in config)\n";
      std::cout << "  --git-shell            Skip admin SSH and assume git-shell access only\n\n";
      std::cout << "Examples:\n";
      std::cout << "  nazg test git merge collision local          # Interactive local test\n";
      std::cout << "  nazg test git merge collision local auto     # Automated local test\n";
      std::cout << "  nazg test git merge collision remote --label myserver\n";
      std::cout << std::endl;
      return 0;
    } else if (arg == "--host" && i + 1 < cctx.argc) {
      options.host = cctx.argv[++i];
      options.host_overridden = true;
    } else if (arg == "--setup-user" && i + 1 < cctx.argc) {
      options.setup_user = cctx.argv[++i];
      options.setup_user_overridden = true;
    } else if (arg == "--git-user" && i + 1 < cctx.argc) {
      options.git_user = cctx.argv[++i];
      options.git_user_overridden = true;
    } else if (arg == "--repo-base" && i + 1 < cctx.argc) {
      options.repo_base = cctx.argv[++i];
      options.repo_base_overridden = true;
    } else if (arg == "--label" && i + 1 < cctx.argc) {
      options.label = cctx.argv[++i];
    } else if (arg == "--project-lang" && i + 1 < cctx.argc) {
      options.project_lang = cctx.argv[++i];
    } else if (arg == "--no-cleanup") {
      options.cleanup = false;
    } else if (arg == "--git-shell") {
      options.git_shell_mode = true;
    } else {
      std::cerr << "Unknown option for merge collision scenario: " << arg << "\n";
      std::cerr << "Run 'nazg test git merge collision --help' for usage\n";
      return 1;
    }
  }

  load_defaults_from_config(options, ectx.cfg, ectx.log);

  // Validate required options for remote mode
  if (options.mode == MergeTestMode::REMOTE) {
    if (options.host.empty()) {
      std::cerr << "Remote mode requires a host.\n";
      std::cerr << "Provide --host or configure a server in config.toml:\n";
      std::cerr << "  [git.servers.myserver]\n";
      std::cerr << "  host = \"example.com\"\n";
      std::cerr << "  ssh_user = \"admin\"\n";
      std::cerr << "  repo_base_path = \"/srv/git\"\n";
      std::cerr << "Then use: --label myserver\n";
      return 1;
    }

    if (!options.git_shell_mode && options.setup_user.empty()) {
      std::cerr << "Remote mode requires --setup-user (SSH user with sudo) or --git-shell mode\n";
      return 1;
    }

    if (options.repo_base.empty()) {
      std::cerr << "Remote mode requires --repo-base or configured repo_base_path\n";
      return 1;
    }

    if (options.git_user.empty()) {
      options.git_user = "git";  // Default for remote
    }

    if (!options.git_shell_mode && options.setup_user == options.git_user) {
      std::cerr << "Configuration uses the git user for administrative operations. Provide an administrative account with sudo access via --setup-user or update git.servers." << options.label << " (ssh_user).\n";
      return 1;
    }
  }

  const std::string repo_name = "merge-demo-" + random_suffix();
  std::string remote_repo_path;
  std::string remote_git_url;

  // Setup paths based on mode
  if (options.mode == MergeTestMode::LOCAL) {
    // For local mode, use temp directory for bare repo
    std::filesystem::path temp_bare = std::filesystem::temp_directory_path() /
                                      ("nazg-bare-" + random_suffix());
    remote_repo_path = (temp_bare / (repo_name + ".git")).string();
    remote_git_url = remote_repo_path;  // Local path is the "URL"
  } else {
    // Remote mode paths
    remote_repo_path = options.repo_base + "/" + repo_name + ".git";
    remote_git_url = options.git_user + "@" + options.host + ":" + remote_repo_path;

    if (!options.git_shell_mode) {
      std::string admin_error;
      if (!check_admin_shell(options, admin_error)) {
        nazg::prompt::Prompt fallback_prompt(ectx.log);
        fallback_prompt.title("SSH Connection Failed")
                      .question("Unable to reach " + options.setup_user + "@" + options.host + ". Use git-shell fallback?")
                      .info("Error: " + (admin_error.empty() ? std::string("ssh failure") : admin_error))
                      .action("Skip remote provisioning and assume bare repo exists at " + remote_repo_path)
                      .action("Proceed as " + options.git_user + " only");

        if (!fallback_prompt.confirm(false)) {
          std::cerr << "Cancelled.\n";
          return 1;
        }

        options.git_shell_mode = true;
        std::cout << "Proceeding in git-shell mode; remote repository must already exist at "
                  << remote_repo_path << " and be writable by " << options.git_user << ".\n";
      }
    }
  }

  std::string lang_lower = options.project_lang;
  for (auto &ch : lang_lower) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (lang_lower != "c" && lang_lower != "cpp" && lang_lower != "python") {
    std::cerr << "Unsupported project language: " << options.project_lang
              << " (supported: c, cpp, python)\n";
    return 1;
  }

  const std::string primary_dir_name = repo_name + "-src";
  const std::string secondary_dir_name = repo_name + "-clone";

  std::filesystem::path temp_root = std::filesystem::temp_directory_path() /
                                    ("nazg-merge-" + random_suffix());
  std::filesystem::create_directories(temp_root);

  std::filesystem::path project_one = temp_root / primary_dir_name;
  std::filesystem::path project_two = temp_root / secondary_dir_name;

  nazg::prompt::ColorFormatter fmt(true);
  nazg::prompt::Gradient header_grad = nazg::prompt::Gradient::cyan_green(7);

  int term_width = nazg::system::term_width();
  if (term_width < 60) term_width = 80;

  // Header box
  std::cout << "\n";
  std::cout << fmt.bold(header_grad.apply_smooth(make_box_top(term_width), fmt)) << "\n";
  std::cout << fmt.bold(header_grad.apply_smooth(make_box_line("Nazg Merge Collision Test Scenario", term_width), fmt)) << "\n";
  std::cout << fmt.bold(header_grad.apply_smooth(make_box_bottom(term_width), fmt)) << "\n\n";

  // Info box
  std::cout << fmt.cyan(make_box_top(term_width)) << "\n";
  std::cout << make_info_line(fmt.dim("Mode:        "), fmt.cyan(options.mode == MergeTestMode::LOCAL ? "local" : "remote"), term_width) << "\n";
  std::cout << make_info_line(fmt.dim("Execution:   "), fmt.yellow(options.auto_mode ? "automated" : "interactive"), term_width) << "\n";
  std::cout << make_info_line(fmt.dim("Repository:  "), fmt.dim(remote_git_url), term_width) << "\n";
  std::cout << make_info_line(fmt.dim("Workspace:   "), fmt.dim(temp_root.string()), term_width) << "\n";
  std::cout << make_info_line(fmt.dim("Language:    "), fmt.magenta(lang_lower), term_width) << "\n";
  std::cout << make_info_line(fmt.dim("Cleanup:     "), (options.cleanup ? fmt.green("yes") : fmt.red("no")), term_width) << "\n";
  std::cout << fmt.cyan(make_box_bottom(term_width)) << "\n";

  if (!options.auto_mode) {
    std::cout << "\n" << fmt.bold("Test Scenario:") << "\n\n";
    std::cout << "  " << fmt.cyan("1.") << " Create a new " << fmt.magenta(lang_lower) << " project\n";
    std::cout << "  " << fmt.cyan("2.") << " Push it to a " << fmt.cyan(options.mode == MergeTestMode::LOCAL ? "local" : "remote") << " repository\n";
    std::cout << "  " << fmt.cyan("3.") << " Clone the repository to a second location\n";
    std::cout << "  " << fmt.cyan("4.") << " Make conflicting changes in both locations\n";
    std::cout << "  " << fmt.cyan("5.") << " Trigger a merge conflict\n";
    std::cout << "  " << fmt.cyan("6.") << " Demonstrate how nazg handles the conflict\n";
  }

  wait_for_user(options, "▶ Ready to begin test scenario");

  // Create repository based on mode
  if (options.mode == MergeTestMode::LOCAL) {
    // Create local bare repository
    std::filesystem::path bare_path(remote_repo_path);
    std::filesystem::create_directories(bare_path.parent_path());

    std::ostringstream cmd;
    cmd << "git init --bare " << nazg::system::shell_quote(remote_repo_path);
    auto res = nazg::system::run_command_capture(cmd.str());

    if (res.exit_code != 0) {
      std::cerr << fmt.red("✗ Failed to create local bare repository: " + res.output) << "\n";
      std::filesystem::remove_all(temp_root);
      return 1;
    }
    std::cout << fmt.green("✓ Created local bare repository at ") << fmt.dim(remote_repo_path) << "\n";
  } else if (!options.git_shell_mode) {
    if (!ensure_remote_repository(options, remote_repo_path)) {
      std::filesystem::remove_all(temp_root);
      return 1;
    }
  } else {
    std::cout << "Skipping remote provisioning due to git-shell mode; ensure the bare repository already exists." << std::endl;
    std::string repo_check_error;
    if (!verify_remote_repository_git_shell(remote_git_url, repo_check_error)) {
      nazg::prompt::Prompt repo_prompt(ectx.log);
      repo_prompt.title("Remote Repository Unreachable")
                .question("nazg could not access " + remote_git_url + ". Continue anyway?")
                .info("Error: " + (repo_check_error.empty() ? std::string("git ls-remote failed") : repo_check_error))
                .action("Continue without remote validation");

      if (!repo_prompt.confirm(false)) {
        std::filesystem::remove_all(temp_root);
        return 1;
      }

      std::cout << "Continuing despite unreachable repository; subsequent pushes may fail." << std::endl;
    }
  }

  std::vector<StepResult> step_log;
  bool aborted = false;

  // Optionally register server through CLI for visibility (remote mode only)
  if (options.mode == MergeTestMode::REMOTE && !options.git_shell_mode && !options.label.empty()) {
    std::ostringstream cmd;
    cmd << "nazg git server add --label " << options.label
        << " --type cgit --host " << options.setup_user << "@" << options.host
        << " --ssh-user " << options.setup_user
        << " --repo-path " << nazg::system::shell_quote(options.repo_base);
    StepResult reg_step = run_shell_step("Register git server", cmd.str(), temp_root);
    if (reg_step.success) {
      step_log.push_back(reg_step);
    } else {
      std::cout << "⚠ Continuing without updating Nazg server registry (command failed).\n";
    }
  }

  // Step 1: scaffold project
  {
    wait_for_user(options, "▶ Step 1: Scaffold " + lang_lower + " project");
    std::ostringstream cmd;
    cmd << "nazg init " << options.project_lang << " " << primary_dir_name
        << " --yes --minimal";
    step_log.push_back(run_shell_step("Scaffold project", cmd.str(), temp_root));
    if (!step_log.back().success) {
      aborted = true;
      goto scenario_cleanup;
    }
  }

  // Ensure project directory exists
  if (!std::filesystem::exists(project_one)) {
    std::cerr << "Expected project directory not found: " << project_one << "\n";
    aborted = true;
    goto scenario_cleanup;
  }

  // Create conflict baseline file
  {
    wait_for_user(options, "▶ Step 2: Create conflict baseline file");
    std::ofstream conflict_file(project_one / "CONFLICT.md", std::ios::trunc);
    conflict_file << "# Merge Test\n\nBase version\n";
  }

  step_log.push_back(run_git_step("Stage initial files", "nazg git add .", project_one));
  if (!step_log.back().success) {
    aborted = true;
    goto scenario_cleanup;
  }

  step_log.push_back(run_git_step("Initial commit", "nazg git commit -m \"Initial commit\"", project_one));
  if (!step_log.back().success) {
    aborted = true;
    goto scenario_cleanup;
  }

  {
    wait_for_user(options, "▶ Step 3: Connect to bare repository");
    std::ostringstream cmd;
    cmd << "nazg git remote add origin " << nazg::system::shell_quote(remote_git_url);
    step_log.push_back(run_git_step("Add remote origin", cmd.str(), project_one));
    if (!step_log.back().success) {
      aborted = true;
      goto scenario_cleanup;
    }
  }

  {
    wait_for_user(options, "▶ Step 4: Push initial content");
    step_log.push_back(run_git_step("Push initial commit", "nazg git push -u origin HEAD", project_one));
    if (!step_log.back().success) {
      if (options.git_shell_mode) {
        std::cout << "⚠ Initial push failed while in git-shell mode. Ensure the bare repository exists at "
                  << remote_repo_path << " and that " << options.git_user
                  << " has write access.\n";
      }
      aborted = true;
      goto scenario_cleanup;
    }
  }

  // Clone second copy
  {
    wait_for_user(options, "▶ Step 5: Clone repository (second developer)");
    std::ostringstream cmd;
    cmd << "nazg git clone " << nazg::system::shell_quote(remote_git_url) << " "
        << nazg::system::shell_quote(project_two.filename().string());
    step_log.push_back(run_shell_step("Clone secondary working copy", cmd.str(), temp_root));
    if (!step_log.back().success) {
      aborted = true;
      goto scenario_cleanup;
    }
  }

  // Ensure clone is checked out
  step_log.push_back(run_git_step("Checkout main branch in clone", "nazg git checkout main", project_two));
  if (!step_log.back().success) {
    aborted = true;
    goto scenario_cleanup;
  }

  // First change in project one
  {
    wait_for_user(options, "▶ Step 6: Make change in first workspace");
    std::ofstream conflict_file(project_one / "CONFLICT.md", std::ios::trunc);
    conflict_file << "# Merge Test\n\nFirst change from primary working copy\n";
  }
  step_log.push_back(run_git_step("Commit change in primary repo",
                                  "nazg git commit -am \"Primary change\"", project_one));
  if (!step_log.back().success) {
    aborted = true;
    goto scenario_cleanup;
  }

  step_log.push_back(run_git_step("Push primary change", "nazg git push", project_one));
  if (!step_log.back().success) {
    aborted = true;
    goto scenario_cleanup;
  }

  // Second change in clone to create conflict
  {
    wait_for_user(options, "▶ Step 7: Make conflicting change in clone");
    std::ofstream conflict_file(project_two / "CONFLICT.md", std::ios::trunc);
    conflict_file << "# Merge Test\n\nConflicting change from secondary working copy\n";
  }

  step_log.push_back(run_git_step("Commit change in clone",
                                  "nazg git commit -am \"Secondary conflicting change\"",
                                  project_two));
  if (!step_log.back().success) {
    aborted = true;
    goto scenario_cleanup;
  }

  {
    wait_for_user(options, "▶ Step 8: Attempt push (should fail)");
    step_log.push_back(run_git_step("Attempt push from clone (expect failure)",
                                    "nazg git push", project_two, /*expected_success=*/false));
    if (!step_log.back().success) {
      // failure expected, so success false indicates unexpected success, handle below
      std::cout << "Push unexpectedly succeeded; merge conflict not triggered.\n";
      aborted = true;
      goto scenario_cleanup;
    }
  }

  // Pull to surface merge conflict
  {
    wait_for_user(options, "▶ Step 9: Pull to surface conflict");
    step_log.push_back(run_git_step("Pull with rebase to surface conflict",
                                    "git pull --rebase origin main", project_two,
                                    /*expected_success=*/false));
  }

  // Display nazg git status for clarity
  {
    wait_for_user(options, "▶ Step 10: Show conflict status");
    step_log.push_back(run_shell_step("Show nazg git status in conflicting repo",
                                      "nazg git status --minimal", project_two));
  }

scenario_cleanup:
  std::cout << "\n\n";
  std::cout << fmt.bold(fmt.cyan(make_box_top(term_width))) << "\n";
  std::cout << fmt.bold(fmt.cyan(make_box_line("Summary", term_width))) << "\n";
  std::cout << fmt.bold(fmt.cyan(make_box_bottom(term_width))) << "\n\n";

  for (const auto &step : step_log) {
    if (step.success) {
      std::cout << "  " << fmt.green("✓ " + step.name);
    } else {
      std::cout << "  " << fmt.red("✗ " + step.name);
      std::cout << fmt.dim(" (exit " + std::to_string(step.exit_code) + ")");
      if (!step.expected_success) {
        std::cout << fmt.yellow(" (unexpected success)");
      }
    }
    if (step.success && !step.expected_success) {
      std::cout << fmt.dim(" (expected failure)");
    }
    std::cout << "\n";
  }

  if (!aborted) {
    std::cout << "\n" << fmt.dim("┌─ Next Steps") << "\n";
    std::cout << fmt.dim("│") << "\n";
    std::cout << fmt.dim("│ Repository: ") << fmt.cyan(project_two.string()) << "\n";
    std::cout << fmt.dim("│") << " " << fmt.yellow("Contains unresolved merge conflict") << "\n";
    std::cout << fmt.dim("│") << "\n";
    std::cout << fmt.dim("│ To resolve:") << "\n";
    std::cout << fmt.dim("│  1. Edit CONFLICT.md to resolve the conflict") << "\n";
    std::cout << fmt.dim("│  2. ") << fmt.cyan("nazg git add CONFLICT.md") << "\n";
    std::cout << fmt.dim("│  3. ") << fmt.cyan("git rebase --continue") << "\n";
    std::cout << fmt.dim("│  4. ") << fmt.cyan("nazg git push") << "\n";
    std::cout << fmt.dim("└" + std::string(term_width - 2, '─')) << "\n";
  } else {
    std::cout << "\n" << fmt.red("✗ Scenario aborted due to failure") << "\n\n";
  }

  // Cleanup logic - always runs
  std::cout << "\n";
  if (options.cleanup) {
    std::cout << fmt.dim("Cleaning up temporary workspace...") << "\n";
    try {
      std::filesystem::remove_all(temp_root);
      std::cout << fmt.green("✓ Cleanup complete") << "\n";
    } catch (const std::exception &e) {
      std::cerr << fmt.yellow("⚠ Cleanup failed: ") << e.what() << "\n";
      std::cerr << fmt.dim("  Workspace: " + temp_root.string()) << "\n";
    }
  } else {
    std::cout << fmt.yellow("Workspace retained at ") << fmt.dim(temp_root.string()) << "\n";
    std::cout << fmt.dim("To clean up manually: rm -rf " + temp_root.string()) << "\n";
  }

  // Determine exit code
  if (aborted) {
    return 1;
  }

  for (const auto &step : step_log) {
    if (!step.success && step.expected_success) {
      return 1;
    }
  }

  if (!aborted && !options.auto_mode) {
    nazg::prompt::Gradient success_grad = nazg::prompt::Gradient::forest(7);
    std::cout << "\n" << fmt.bold(success_grad.apply_smooth("✅ Test completed successfully!", fmt)) << "\n";
    std::cout << fmt.dim("The merge conflict scenario demonstrated:") << "\n";
    std::cout << "  " << fmt.green("•") << " How nazg scaffolds projects\n";
    std::cout << "  " << fmt.green("•") << " Working with bare git repositories\n";
    std::cout << "  " << fmt.green("•") << " Detecting merge conflicts\n";
    std::cout << "  " << fmt.green("•") << " Displaying conflict status\n";
    std::cout << "\n";
  }

  return 0;
}

// ── Prompt Test Suite ──

int run_prompt_capability_test(const nazg::directive::context &ectx) {
  std::cout << "\n";
  std::cout << "╭─────────────────────────────────────────────────────────────╮\n";
  std::cout << "│ Terminal Capability Detection                               │\n";
  std::cout << "╰─────────────────────────────────────────────────────────────╯\n";
  std::cout << "\n";

  auto caps = nazg::system::get_capabilities();

  std::cout << "Detection Results:\n";
  std::cout << "─────────────────────────────────────────\n";
  std::cout << "  TTY:                " << (caps.is_tty ? "✓ Yes" : "✗ No") << "\n";
  std::cout << "  Unicode Support:    " << (caps.supports_unicode ? "✓ Yes" : "✗ No") << "\n";
  std::cout << "  Terminal Width:     " << caps.width << " columns\n";
  std::cout << "  Terminal Height:    " << caps.height << " rows\n";
  std::cout << "\n";

  std::cout << "Color Support Level:\n";
  std::cout << "─────────────────────────────────────────\n";

  std::string color_level;
  switch (caps.color_support) {
    case nazg::system::ColorSupport::TRUE_COLOR:
      color_level = "✓ TRUE COLOR (24-bit RGB, 16 million colors)";
      break;
    case nazg::system::ColorSupport::ANSI_256:
      color_level = "✓ 256 COLORS";
      break;
    case nazg::system::ColorSupport::ANSI_16:
      color_level = "✓ 16 COLORS";
      break;
    case nazg::system::ColorSupport::ANSI_8:
      color_level = "✓ 8 COLORS (basic ANSI)";
      break;
    case nazg::system::ColorSupport::NONE:
      color_level = "✗ NO COLOR SUPPORT";
      break;
  }

  std::cout << "  " << color_level << "\n";
  std::cout << "\n";

  std::cout << "Environment Variables:\n";
  std::cout << "─────────────────────────────────────────\n";
  const char* env_vars[] = {"TERM", "COLORTERM", "TERM_PROGRAM", "NO_COLOR", "FORCE_COLOR"};
  for (const char* var : env_vars) {
    const char* value = std::getenv(var);
    std::cout << "  " << std::left << std::setw(15) << var << " = " << (value ? value : "(not set)") << "\n";
  }

  std::cout << "\n";
  return 0;
}

int run_prompt_color_test(const nazg::directive::context &ectx) {
  nazg::prompt::ColorFormatter fmt(nazg::system::get_capabilities());

  std::cout << "\n";
  std::cout << "╭─────────────────────────────────────────────────────────────╮\n";
  std::cout << "│ Nazg Color System Test                                      │\n";
  std::cout << "╰─────────────────────────────────────────────────────────────╯\n";
  std::cout << "\n";

  // Show capability
  auto caps = nazg::system::get_capabilities();
  std::cout << "Your terminal supports: ";
  switch (caps.color_support) {
    case nazg::system::ColorSupport::TRUE_COLOR:
      std::cout << fmt.rgb(136, 192, 208, "TRUE COLOR") << " (24-bit RGB)\n";
      break;
    case nazg::system::ColorSupport::ANSI_256:
      std::cout << fmt.c256(51, "256 COLORS") << "\n";
      break;
    case nazg::system::ColorSupport::ANSI_16:
      std::cout << fmt.cyan("16 COLORS") << "\n";
      break;
    case nazg::system::ColorSupport::ANSI_8:
      std::cout << fmt.cyan("8 COLORS") << "\n";
      break;
    case nazg::system::ColorSupport::NONE:
      std::cout << "NO COLORS\n";
      break;
  }
  std::cout << "\n";

  // Test basic colors
  std::cout << "Basic ANSI Colors (16-color):\n";
  std::cout << "─────────────────────────────────────────\n";
  std::cout << "  " << fmt.red("Red") << " "
            << fmt.green("Green") << " "
            << fmt.yellow("Yellow") << " "
            << fmt.blue("Blue") << " "
            << fmt.magenta("Magenta") << " "
            << fmt.cyan("Cyan") << " "
            << fmt.gray("Gray") << " "
            << fmt.white("White") << "\n";
  std::cout << "  " << fmt.bold("Bold") << " "
            << fmt.dim("Dim") << "\n";
  std::cout << "\n";

  // Test RGB colors
  if (caps.color_support >= nazg::system::ColorSupport::ANSI_16) {
    std::cout << "RGB Colors (auto-adapted to your terminal):\n";
    std::cout << "─────────────────────────────────────────\n";
    std::cout << "  " << fmt.rgb(255, 0, 0, "Pure Red") << " "
              << fmt.rgb(0, 255, 0, "Pure Green") << " "
              << fmt.rgb(0, 0, 255, "Pure Blue") << "\n";
    std::cout << "  " << fmt.rgb(255, 165, 0, "Orange") << " "
              << fmt.rgb(255, 192, 203, "Pink") << " "
              << fmt.rgb(128, 0, 128, "Purple") << "\n";
    std::cout << "\n";
  }

  // Test hex colors
  if (caps.color_support >= nazg::system::ColorSupport::ANSI_16) {
    std::cout << "Hex Colors (#RRGGBB):\n";
    std::cout << "─────────────────────────────────────────\n";
    std::cout << "  " << fmt.hex("#5E81AC", "Nord Blue") << " "
              << fmt.hex("#88C0D0", "Nord Cyan") << " "
              << fmt.hex("#A3BE8C", "Nord Green") << "\n";
    std::cout << "  " << fmt.hex("#BF616A", "Nord Red") << " "
              << fmt.hex("#EBCB8B", "Nord Yellow") << " "
              << fmt.hex("#B48EAD", "Nord Purple") << "\n";
    std::cout << "\n";
  }

  // Test gradients
  if (caps.color_support >= nazg::system::ColorSupport::ANSI_16) {
    std::cout << "Gradients:\n";
    std::cout << "─────────────────────────────────────────\n";

    // Cyan to green gradient
    nazg::prompt::Gradient grad1(nazg::prompt::Color(0, 255, 255), nazg::prompt::Color(0, 255, 0), 20);
    std::cout << "  Cyan→Green:  " << grad1.apply("████████████████████", fmt) << "\n";

    // Fire gradient
    auto fire = nazg::prompt::Gradient::fire(20);
    std::cout << "  Fire:        " << fire.apply("████████████████████", fmt) << "\n";

    // Ocean gradient
    auto ocean = nazg::prompt::Gradient::ocean(20);
    std::cout << "  Ocean:       " << ocean.apply("████████████████████", fmt) << "\n";

    // Sunset gradient
    auto sunset = nazg::prompt::Gradient::sunset(20);
    std::cout << "  Sunset:      " << sunset.apply("████████████████████", fmt) << "\n";

    std::cout << "\n";
  }

  // Test progress bars with gradients
  if (caps.color_support >= nazg::system::ColorSupport::ANSI_16) {
    std::cout << "Progress Bars (with gradients):\n";
    std::cout << "─────────────────────────────────────────\n";

    auto grad = nazg::prompt::Gradient(nazg::prompt::Color(94, 129, 172), nazg::prompt::Color(163, 190, 140), 30);
    std::cout << "  25%  " << grad.progress_bar(30, 0.25f, fmt) << "\n";
    std::cout << "  50%  " << grad.progress_bar(30, 0.50f, fmt) << "\n";
    std::cout << "  75%  " << grad.progress_bar(30, 0.75f, fmt) << "\n";
    std::cout << "  100% " << grad.progress_bar(30, 1.00f, fmt) << "\n";

    std::cout << "\n";
  }

  // Test ANSI stripping
  std::cout << "ANSI Code Utilities:\n";
  std::cout << "─────────────────────────────────────────\n";
  std::string colored = fmt.rgb(255, 0, 0, "Colored") + " " + fmt.blue("Text");
  std::string stripped = nazg::prompt::ColorFormatter::strip_ansi(colored);
  std::cout << "  Colored:  " << colored << "\n";
  std::cout << "  Stripped: " << stripped << "\n";
  std::cout << "  Width:    " << nazg::prompt::ColorFormatter::display_width(colored) << " characters\n";

  std::cout << "\n";
  std::cout << "✨ Color system working perfectly!\n";
  std::cout << "\n";

  return 0;
}

// Command: nazg test
int cmd_test(const nazg::directive::command_context &cctx,
             const nazg::directive::context &ectx) {

  // Show help for top-level: nazg test
  if (cctx.argc == 2 || (cctx.argc == 3 && (std::string(cctx.argv[2]) == "--help" || std::string(cctx.argv[2]) == "-h"))) {
    std::cout << "Usage: nazg test [suite] [options]\n\n";
    std::cout << "Available test suites:\n";
    std::cout << "  git                    Git functionality tests\n";
    std::cout << "  prompt                 Prompt system visual tests\n";
    std::cout << "  workspace              Workspace snapshot & restore tests\n";
    std::cout << "  framework              Test runner framework tests (auto-detect project)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  nazg test git          List all git tests\n";
    std::cout << "  nazg test prompt       List all prompt tests\n";
    std::cout << "  nazg test framework    Run project tests (pytest, cargo, jest, etc.)\n";
    std::cout << "  nazg test --help       Show this help\n";
    return 0;
  }

  // Route to suites
  if (cctx.argc >= 3) {
    std::string suite = cctx.argv[2];

    // Git test suite
    if (suite == "git") {
      // Show git test suite help
      if (cctx.argc == 3 || (cctx.argc == 4 && (std::string(cctx.argv[3]) == "--help" || std::string(cctx.argv[3]) == "-h"))) {
        std::cout << "Usage: nazg test git [test-name] [options]\n\n";
        std::cout << "Available git tests:\n";
        std::cout << "  merge collision        Test merge conflict detection and handling\n\n";
        std::cout << "Examples:\n";
        std::cout << "  nazg test git merge collision local     Run with local bare repo\n";
        std::cout << "  nazg test git merge collision remote    Run with remote git server\n";
        return 0;
      }

      // Route to specific git tests
      if (cctx.argc >= 4) {
        std::string test_name = cctx.argv[3];
        if (test_name == "merge" && cctx.argc >= 5 && std::string(cctx.argv[4]) == "collision") {
          return run_git_merge_collision_scenario(cctx, ectx, 5);
        } else if (test_name == "merge-collision") {
          return run_git_merge_collision_scenario(cctx, ectx, 4);
        }
      }

      std::cerr << "Unknown git test. Run 'nazg test git --help' for available tests.\n";
      return 1;
    }

    // Prompt test suite
    if (suite == "prompt") {
      // Show prompt test suite help
      if (cctx.argc == 3 || (cctx.argc == 4 && (std::string(cctx.argv[3]) == "--help" || std::string(cctx.argv[3]) == "-h"))) {
        std::cout << "Usage: nazg test prompt [test-name]\n\n";
        std::cout << "Available prompt tests:\n";
        std::cout << "  colors                 Test color system (RGB, 256, gradients)\n";
        std::cout << "  capability             Show terminal capabilities\n\n";
        std::cout << "Examples:\n";
        std::cout << "  nazg test prompt colors       Test all color features\n";
        std::cout << "  nazg test prompt capability   Show what your terminal supports\n";
        return 0;
      }

      // Route to specific prompt tests
      if (cctx.argc >= 4) {
        std::string test_name = cctx.argv[3];
        if (test_name == "colors") {
          return run_prompt_color_test(ectx);
        } else if (test_name == "capability") {
          return run_prompt_capability_test(ectx);
        }
      }

      std::cerr << "Unknown prompt test. Run 'nazg test prompt --help' for available tests.\n";
      return 1;
    }

    // Workspace test suite
    if (suite == "workspace") {
      auto print_workspace_help = []() {
        std::cout << "Usage: nazg test workspace [test-name]\n\n";
        std::cout << "Available workspace tests:\n";
        std::cout << "  snapshot-creation      Snapshot metadata & tagging flow\n";
        std::cout << "  prune-behavior         Snapshot pruning safety checks\n";
        std::cout << "  env-capture            Environment variable capture\n";
        std::cout << "  restore-full           Full snapshot restore round-trip\n\n";
        std::cout << "Examples:\n";
        std::cout << "  nazg test workspace              Run all workspace tests\n";
        std::cout << "  nazg test workspace restore-full Run only the restore test\n";
      };

      auto logger = ectx.log;
      nazg::blackbox::options fallback_opts;
      std::unique_ptr<nazg::blackbox::logger> fallback_logger;
      if (!logger) {
        fallback_opts.console_enabled = true;
        fallback_logger = std::make_unique<nazg::blackbox::logger>(fallback_opts);
        logger = fallback_logger.get();
      }

      auto run_case = [&](const std::string &name,
                          bool (*fn)(nazg::blackbox::logger *, std::string &)) {
        std::string error;
        bool ok = fn(logger, error);
        if (!ok) {
          if (!error.empty()) {
            std::cerr << "✗ " << name << ": " << error << "\n";
          } else {
            std::cerr << "✗ " << name << " failed\n";
          }
        } else {
          std::cout << "✓ " << name << " passed\n";
        }
        return ok;
      };

      if (cctx.argc == 3) {
        bool all_ok = true;
        all_ok &= run_case("snapshot-creation", nazg::test::workspace_suite::run_snapshot_creation);
        all_ok &= run_case("prune-behavior", nazg::test::workspace_suite::run_prune_behavior);
        all_ok &= run_case("env-capture", nazg::test::workspace_suite::run_env_capture);
        all_ok &= run_case("restore-full", nazg::test::workspace_suite::run_restore_full);
        return all_ok ? 0 : 1;
      }

      if (cctx.argc == 4 && (std::string(cctx.argv[3]) == "--help" || std::string(cctx.argv[3]) == "-h")) {
        print_workspace_help();
        return 0;
      }

      std::string test_name = cctx.argv[3];
      if (test_name == "snapshot-creation") {
        return run_case(test_name, nazg::test::workspace_suite::run_snapshot_creation) ? 0 : 1;
      }
      if (test_name == "prune-behavior") {
        return run_case(test_name, nazg::test::workspace_suite::run_prune_behavior) ? 0 : 1;
      }
      if (test_name == "env-capture") {
        return run_case(test_name, nazg::test::workspace_suite::run_env_capture) ? 0 : 1;
      }
      if (test_name == "restore-full") {
        return run_case(test_name, nazg::test::workspace_suite::run_restore_full) ? 0 : 1;
      }

      std::cerr << "Unknown workspace test. Run 'nazg test workspace --help' for available tests.\n";
      return 1;
    }

    // Framework test suite (existing test runner functionality)
    if (suite == "framework" || suite == "--coverage" || suite == "--filter" || suite == "-j" || suite == "--fail-fast" || suite == "--verbose") {
      // Run existing framework test logic
      std::string cwd = get_cwd();
      nazg::brain::Detector detector(ectx.store, ectx.log);
      auto info = detector.detect(cwd);

      if (!info.has_tests) {
        std::cerr << "❌ No tests detected in this project\n";
        std::cerr << "   Supported frameworks: GTest, pytest, cargo test, Jest, Go\n";
        return 1;
      }

      nazg::test::RunOptions opts;
      opts.verbose = ectx.verbose;

      // Start parsing from index 3 if "framework" was explicit, else from 2
      int start_idx = (suite == "framework") ? 3 : 2;

      for (int i = start_idx; i < cctx.argc; ++i) {
        std::string arg = cctx.argv[i];

        if (arg == "--help" || arg == "-h") {
          std::cout << "Usage: nazg test framework [options]\n\n";
          std::cout << "Options:\n";
          std::cout << "  --coverage          Collect code coverage\n";
          std::cout << "  --filter PATTERN    Run only tests matching pattern\n";
          std::cout << "  -j N                Run N tests in parallel\n";
          std::cout << "  --fail-fast         Stop on first failure\n";
          std::cout << "  --verbose           Verbose output\n";
          std::cout << "  -h, --help          Show this help\n";
          return 0;
        } else if (arg == "--coverage") {
          opts.collect_coverage = true;
        } else if (arg == "--filter" && i + 1 < cctx.argc) {
          opts.filter = cctx.argv[++i];
        } else if (arg == "-j" && i + 1 < cctx.argc) {
          opts.parallel_jobs = std::atoi(cctx.argv[++i]);
        } else if (arg == "--fail-fast") {
          opts.fail_fast = true;
        } else if (arg == "--verbose") {
          opts.verbose = true;
        } else {
          std::cerr << "Unknown option: " << arg << "\n";
          std::cerr << "Run 'nazg test framework --help' for usage\n";
          return 1;
        }
      }

      nazg::brain::Planner planner(ectx.store, ectx.log);
      auto plan = planner.generate_test_plan(info);

      if (plan.action != nazg::brain::Action::TEST) {
        std::cerr << "❌ Failed to generate test plan\n";
        if (!plan.reason.empty()) {
          std::cerr << "   " << plan.reason << "\n";
        }
        return 1;
      }

      nazg::test::Runner runner(ectx.store, ectx.log);
      int64_t project_id = ectx.store->ensure_project(cwd);

      std::cout << "🧪 Running tests using " << info.test_framework << "...\n";

      auto result = runner.execute(project_id, plan, opts);

      std::cout << "\n";
      std::cout << "═══════════════════════════════════════════\n";

      if (result.failed == 0 && result.errors == 0) {
        std::cout << "✅ All tests passed!\n";
      } else {
        std::cout << "❌ Some tests failed\n";
      }

      std::cout << "\n";
      std::cout << "Tests:    " << result.total << " total\n";
      std::cout << "Passed:   " << result.passed << " (" << (result.total > 0 ? (result.passed * 100 / result.total) : 0) << "%)\n";

      if (result.failed > 0) {
        std::cout << "Failed:   " << result.failed << "\n";
      }
      if (result.skipped > 0) {
        std::cout << "Skipped:  " << result.skipped << "\n";
      }
      if (result.errors > 0) {
        std::cout << "Errors:   " << result.errors << "\n";
      }

      std::cout << "Duration: " << (result.duration_ms / 1000.0) << "s\n";

      if (result.coverage) {
        std::cout << "Coverage: " << (result.coverage->line_coverage * 100.0) << "% lines\n";
      }

      std::cout << "═══════════════════════════════════════════\n";

      return (result.failed > 0 || result.errors > 0) ? 1 : 0;
    }
  }

  std::cerr << "Unknown test suite. Run 'nazg test --help' for available suites.\n";
  return 1;
}

// Command: nazg test-results
int cmd_test_results(const nazg::directive::command_context &cctx,
                     const nazg::directive::context &ectx) {
  std::string cwd = get_cwd();
  int64_t project_id = ectx.store->ensure_project(cwd);

  bool show_failed_only = false;
  int limit = 1;  // Show last run by default

  for (int i = 2; i < cctx.argc; ++i) {
    std::string arg = cctx.argv[i];

    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg test-results [options]\n\n";
      std::cout << "Options:\n";
      std::cout << "  --failed       Show only failed tests\n";
      std::cout << "  --limit N      Show last N test runs (default: 1)\n";
      std::cout << "  -h, --help     Show this help\n";
      return 0;
    } else if (arg == "--failed") {
      show_failed_only = true;
    } else if (arg == "--limit" && i + 1 < cctx.argc) {
      limit = std::atoi(cctx.argv[++i]);
    }
  }

  auto runs = ectx.store->get_test_runs(project_id, limit);

  if (runs.empty()) {
    std::cout << "No test runs found for this project\n";
    return 0;
  }

  for (const auto &run_map : runs) {
    int64_t run_id = std::stoll(run_map.at("id"));
    std::string framework = run_map.at("framework");
    int total = std::stoi(run_map.at("total_tests"));
    int passed = std::stoi(run_map.at("passed"));
    int failed = std::stoi(run_map.at("failed"));

    std::cout << "\n";
    std::cout << "Test Run #" << run_id << " (" << framework << ")\n";
    std::cout << "─────────────────────────────────────\n";
    std::cout << "Total:  " << total << "\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    // Get test results
    std::vector<std::map<std::string, std::string>> results;
    if (show_failed_only) {
      results = ectx.store->get_failed_test_results(run_id);
    } else {
      results = ectx.store->get_test_results(run_id);
    }

    if (!results.empty()) {
      std::cout << "\nTest Cases:\n";
      for (const auto &test_map : results) {
        std::string name = test_map.at("name");
        std::string status = test_map.at("status");
        std::string message = test_map.at("message");

        std::cout << "  ";
        if (status == "passed") {
          std::cout << "✓ ";
        } else if (status == "failed") {
          std::cout << "✗ ";
        } else {
          std::cout << "○ ";
        }

        std::cout << name << " [" << status << "]\n";

        if (!message.empty() && status != "passed") {
          std::cout << "    " << message << "\n";
        }
      }
    }
  }

  return 0;
}

}  // namespace

namespace nazg::test {

void register_commands(directive::registry &reg, directive::context &ctx) {
  (void)ctx;  // Context not used in registration

  reg.add("test", "Run project tests", cmd_test);
  reg.add("test-results", "Show recent test results", cmd_test_results);
}

}  // namespace nazg::test

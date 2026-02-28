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

#include "bot/commands.hpp"
#include "bot/registry.hpp"
#include "bot/types.hpp"
#include "bot/transport.hpp"
#include "directive/context.hpp"
#include "directive/registry.hpp"
#include "blackbox/logger.hpp"
#include "config/config.hpp"
#include "nexus/store.hpp"
#include "prompt/prompt.hpp"
#include "system/package.hpp"
#include "system/process.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string_view>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace nazg::bot {

namespace {

struct FailureHint {
  std::string summary;
  std::vector<std::string> tips;
};

struct HostRecord {
  int64_t id = 0;
  std::string label;
  std::string address;
  std::string ssh_config;
  std::string last_status;
  std::string last_run_at;
};

struct SshConfigFields {
  std::string key;
  int port = 22;
  int agent_port = 7070;
};

struct PromptFlags {
  bool force_yes = false;
  bool force_no = false;
  prompt::Style style = prompt::Style::STANDARD;
};

using CommandFn = directive::command_spec::fn_t;

static std::string to_lower_copy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

static std::string map_get(const std::map<std::string, std::string> &row,
                           const std::string &key,
                           const std::string &fallback = {}) {
  auto it = row.find(key);
  return it != row.end() ? it->second : fallback;
}

FailureHint diagnose_ssh_failure(const HostConfig &host, const std::string &output,
                                 int exit_code) {
  FailureHint hint;
  std::string lower = to_lower_copy(output);

  auto contains = [&](std::string_view needle) {
    return !needle.empty() && lower.find(needle) != std::string::npos;
  };

  bool has_username = host.address.find('@') != std::string::npos;
  auto add_username_tip = [&]() {
    if (!has_username) {
      hint.tips.push_back("Specify the remote account explicitly, e.g. nazg bot setup --host user@" + host.address);
    }
  };

  if (contains("permission denied")) {
    hint.summary = "Authentication failed";
    add_username_tip();
    hint.tips.push_back("Double-check the password (remote host may warn after multiple attempts)");
    hint.tips.push_back("Ensure PasswordAuthentication yes on the remote /etc/ssh/sshd_config and restart sshd");
    hint.tips.push_back("If keys already exist for another user, remove stale entries from ~/.ssh/known_hosts");
    hint.tips.push_back("You can test manually: ssh user@host and confirm password login works");
    return hint;
  }

  if (contains("could not resolve hostname")) {
    hint.summary = "Hostname could not be resolved";
    hint.tips = {
        "Confirm the host/IP is correct",
        "Check DNS resolution or add an entry to /etc/hosts"};
    return hint;
  }

  if (contains("no route to host") || contains("network is unreachable")) {
    hint.summary = "Network path unavailable";
    hint.tips = {
        "Verify the host is reachable on the network",
        "Check VPN / firewall settings"};
    return hint;
  }

  if (contains("connection refused")) {
    hint.summary = "SSH service refused connection";
    hint.tips = {
        "Ensure sshd is running on the remote host",
        "Confirm the port (use --port to override if not 22)"};
    return hint;
  }

  if (contains("operation timed out") || contains("connection timed out")) {
    hint.summary = "Connection timed out";
    hint.tips = {
        "Host may be offline or behind a firewall",
        "Check that port 22 (or your custom port) is reachable"};
    return hint;
  }

  if (contains("host key verification failed") || contains("remote host identification has changed")) {
    hint.summary = "Host key verification failed";
    hint.tips = {
        "Remove the stale entry from ~/.ssh/known_hosts",
        "Ensure you trust the remote host before retrying"};
    return hint;
  }

  if (contains("too many authentication failures")) {
    hint.summary = "Too many authentication attempts";
    hint.tips = {
        "Clear agent identities (ssh-add -D) or limit offered keys",
        "Retry after adjusting SSH configuration"};
    return hint;
  }

  if (contains("ssh_exchange_identification")) {
    hint.summary = "SSH exchange was closed";
    hint.tips = {
        "Check remote sshd logs for bannings (e.g., Fail2ban)",
        "Verify your IP is allowed to connect"};
    return hint;
  }

  if (!output.empty()) {
    hint.summary = "SSH command failed";
    hint.tips = {"Review the output above for more detail"};
  } else {
    hint.summary = "SSH command exited with code " + std::to_string(exit_code);
    hint.tips = {"Check network connectivity and remote SSH configuration"};
  }

  return hint;
}

std::string format_timestamp(const std::string &epoch_str) {
  if (epoch_str.empty())
    return "-";

  try {
    int64_t epoch = std::stoll(epoch_str);
    if (epoch <= 0)
      return "-";

    std::time_t tt = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm)) {
      return buffer;
    }
  } catch (...) {
  }
  return epoch_str;
}

std::string format_duration_ms(const std::string &ms_str) {
  if (ms_str.empty())
    return "-";
  try {
    int64_t ms = std::stoll(ms_str);
    if (ms < 1000)
      return ms_str + "ms";
    double seconds = static_cast<double>(ms) / 1000.0;
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(seconds >= 10 ? 1 : 2) << seconds << "s";
    return oss.str();
  } catch (...) {
    return ms_str;
  }
}

static HostRecord host_from_row(const std::map<std::string, std::string> &row) {
  HostRecord host;
  try {
    host.id = std::stoll(map_get(row, "id", "0"));
  } catch (...) {
    host.id = 0;
  }
  host.label = map_get(row, "label");
  host.address = map_get(row, "address");
  host.ssh_config = map_get(row, "ssh_config");
  host.last_status = map_get(row, "last_status");
  host.last_run_at = map_get(row, "last_run_at");
  return host;
}

static std::string escape_json_string(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 2);
  for (char ch : value) {
    if (ch == '"' || ch == '\\') {
      out.push_back('\\');
    }
    out.push_back(ch);
  }
  return out;
}

static std::string extract_json_token(const std::string &json, std::string_view key) {
  auto pos = json.find('"' + std::string(key) + '"');
  if (pos == std::string::npos)
    return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos)
    return {};
  ++pos;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
    ++pos;
  if (pos >= json.size())
    return {};

  if (json[pos] == '"') {
    ++pos;
    std::string value;
    while (pos < json.size()) {
      char ch = json[pos++];
      if (ch == '\\' && pos < json.size()) {
        value.push_back(json[pos++]);
        continue;
      }
      if (ch == '"')
        break;
      value.push_back(ch);
    }
    return value;
  }

  size_t start = pos;
  while (pos < json.size() && (std::isdigit(static_cast<unsigned char>(json[pos])) ||
                               json[pos] == '-' || json[pos] == '+')) {
    ++pos;
  }
  return json.substr(start, pos - start);
}

static SshConfigFields parse_ssh_config_json(const std::string &json) {
  SshConfigFields cfg;
  if (json.empty())
    return cfg;

  std::string key = extract_json_token(json, "key");
  if (!key.empty())
    cfg.key = key;

  std::string port = extract_json_token(json, "port");
  if (!port.empty()) {
    try {
      cfg.port = std::stoi(port);
    } catch (...) {
    }
  }

  std::string agent_port = extract_json_token(json, "agent_port");
  if (!agent_port.empty()) {
    try {
      cfg.agent_port = std::stoi(agent_port);
    } catch (...) {
    }
  }

  return cfg;
}

static std::string build_ssh_config_json(const SshConfigFields &cfg) {
  std::ostringstream oss;
  oss << "{\"key\":\"" << escape_json_string(cfg.key) << "\""
      << ",\"port\":" << cfg.port
      << ",\"agent_port\":" << cfg.agent_port << "}";
  return oss.str();
}

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

static int forward_bot_command(const directive::command_context &ctx,
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

  std::vector<const char *> argv;
  argv.reserve(storage.size());
  for (auto &s : storage) {
    argv.push_back(s.c_str());
  }

  directive::command_context sub_ctx;
  sub_ctx.argc = static_cast<int>(argv.size());
  sub_ctx.argv = argv.data();
  return fn(sub_ctx, ectx);
}

static void print_bot_help(const char *prog) {
  std::string program = prog ? prog : "nazg";
  std::cout << "Usage: " << program << " bot <subcommand> [options]\n\n";
  std::cout << "Available subcommands:\n";
  std::cout << "  hosts             List registered bot hosts\n";
  std::cout << "  history           Show recorded bot runs (filters available)\n";
  std::cout << "  manage            Interactive host editor (rename/update/delete)\n";
  std::cout << "  setup             Configure SSH access for a host\n";
  std::cout << "  spawn <bot>       Run a bot by name\n";
  std::cout << "  doctor            Shortcut for 'spawn doctor'\n";
  std::cout << "  git-doctor        Shortcut for 'spawn git-doctor'\n";
  std::cout << "  list              Summarise recent bot runs\n";
  std::cout << "  report <bot>      Show the latest report for a host\n";
  std::cout << "\nExamples:\n";
  std::cout << "  " << program << " bot doctor --host user@server\n";
  std::cout << "  " << program << " bot git-doctor --host user@gitserver\n";
  std::cout << "  " << program << " bot setup --host 10.0.0.4\n";
  std::cout << "  " << program << " bot history --host prod --limit 10\n";
}

// Read password from stdin with hidden input
std::string read_password(const std::string& prompt_text) {
  std::cout << prompt_text << ": " << std::flush;

  // Turn off terminal echo
  termios old_term, new_term;
  tcgetattr(STDIN_FILENO, &old_term);
  new_term = old_term;
  new_term.c_lflag &= ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

  // Read password
  std::string password;
  std::getline(std::cin, password);

  // Restore terminal settings
  tcsetattr(STDIN_FILENO, TCSANOW, &old_term);

  std::cout << "\n";
  return password;
}

// Parse host config from command args or config
HostConfig parse_host_config(const directive::command_context& ctx,
                               const directive::context& ectx,
                               int start_index) {
  HostConfig host;

  // Parse command line arguments
  for (int i = start_index; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--host" && i + 1 < ctx.argc) {
      host.address = ctx.argv[i + 1];
      i++;
    } else if (arg == "--label" && i + 1 < ctx.argc) {
      host.label = ctx.argv[i + 1];
      i++;
    } else if (arg == "--key" && i + 1 < ctx.argc) {
      host.ssh_key = ctx.argv[i + 1];
      i++;
    } else if (arg == "--port" && i + 1 < ctx.argc) {
      host.ssh_port = std::stoi(ctx.argv[i + 1]);
      i++;
    } else if (arg == "--service" && i + 1 < ctx.argc) {
      host.services.push_back(ctx.argv[i + 1]);
      i++;
    } else if (arg == "--agent-port" && i + 1 < ctx.argc) {
      host.agent_port = std::stoi(ctx.argv[++i]);
    }
  }

  // If label not provided, use address as label
  if (host.label.empty()) {
    host.label = host.address;
  }

  // Try to load from config if label matches a configured host
  if (ectx.cfg && ectx.cfg->has("bots.hosts." + host.label, "address")) {
    std::string addr = ectx.cfg->get_string("bots.hosts." + host.label, "address");
    if (host.address.empty()) {
      host.address = addr;
    }
    // Load other config values
    if (ectx.cfg->has("bots.hosts." + host.label, "ssh_key")) {
      host.ssh_key = ectx.cfg->get_string("bots.hosts." + host.label, "ssh_key");
    }
    if (ectx.cfg->has("bots.hosts." + host.label, "services")) {
      // Parse services from config (would need better parsing in production)
      std::string services_str = ectx.cfg->get_string("bots.hosts." + host.label, "services");
      // Simple comma-separated parsing
      std::istringstream ss(services_str);
      std::string service;
      while (std::getline(ss, service, ',')) {
        host.services.push_back(service);
      }
    }
    if (ectx.cfg->has("bots.hosts." + host.label, "agent_port")) {
      host.agent_port = ectx.cfg->get_int("bots.hosts." + host.label, "agent_port", host.agent_port);
    }
  }

  // Apply global bot defaults from config
  if (ectx.cfg) {
    if (host.ssh_key.empty() && ectx.cfg->has("bots", "ssh_key")) {
      host.ssh_key = ectx.cfg->get_string("bots", "ssh_key");
    }
    if (ectx.cfg->has("bots", "agent_port")) {
      host.agent_port = ectx.cfg->get_int("bots", "agent_port", host.agent_port);
    }
  }

  return host;
}

} // namespace

static int cmd_bot_setup(const directive::command_context& ctx,
                         const directive::context& ectx);

// nazg bot spawn <bot-name> --host <target>
static int cmd_bot_spawn(const directive::command_context& ctx,
                          const directive::context& ectx) {
  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg bot spawn <bot-name> --host <target> [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --host <address>   SSH address (user@host or host)\n";
    std::cerr << "  --label <name>     Host label (defaults to address)\n";
    std::cerr << "  --key <path>       SSH key path\n";
    std::cerr << "  --port <number>    SSH port (default: 22)\n";
    std::cerr << "  --service <name>   Service to monitor (can be repeated)\n";
    std::cerr << "  --agent-port <n>   Remote agent port (default: 7070)\n";
    return 1;
  }

  std::string bot_name = ctx.argv[2];
  HostConfig host = parse_host_config(ctx, ectx, 3);

  if (host.address.empty()) {
    std::cerr << "Error: --host is required\n";
    return 1;
  }

  // Ensure supporting tooling is available for doctor bot flows (key distribution, etc.).
  if (bot_name == "doctor" && !::nazg::system::is_package_installed("sshpass")) {
    prompt::Prompt pkg_prompt(ectx.log);
    if (!::nazg::system::install_package("sshpass", &pkg_prompt, ectx.log)) {
      std::cerr << "\nWarning: sshpass not installed; bot setup assistance may fail.\n";
    }
  }

  // Get or create bot registry
  auto& reg = get_registry();
  register_builtin_bots(reg);

  // Check if bot exists
  auto spec = reg.get_spec(bot_name);
  if (!spec) {
    std::cerr << "Error: Unknown bot '" << bot_name << "'\n";
    std::cerr << "\nAvailable bots:\n";
    for (const auto& s : reg.list_bots()) {
      std::cerr << "  " << s.name << " - " << s.description << "\n";
    }
    return 1;
  }

  // Ensure database is available
  if (!ectx.store) {
    std::cerr << "Fatal: Database not initialized (engine bootstrap failed)\n";
    std::cerr << "Check for errors during startup. The database should have been initialized automatically.\n";
    return 1;
  }

  // Ensure host exists in database
  int64_t host_id = 0;
  if (auto existing_id = ectx.store->get_bot_host_id(host.label)) {
    host_id = *existing_id;
  } else {
    // Create host record
    std::ostringstream ssh_config;
    ssh_config << "{\"key\":\"" << host.ssh_key << "\",\"port\":" << host.ssh_port
               << ",\"agent_port\":" << host.agent_port << "}";
    host_id = ectx.store->add_bot_host(host.label, host.address, ssh_config.str());
  }

  host.id = host_id;

  if ((bot_name == "doctor" || bot_name == "git-doctor") && ectx.store) {
    if (!host.label.empty()) {
      if (auto server = ectx.store->get_git_server(host.label)) {
        host.extra_config["git_server_label"] = server->label;
        host.extra_config["git_server_type"] = server->type;
        host.extra_config["repo_base_path"] = server->repo_base_path;
        host.extra_config["config_path"] = server->config_path;
      }
    }
  }

  if (ectx.log) {
    ectx.log->info("bot", "Spawning " + bot_name + " on " + host.label);
  }

  bool agent_available = false;
  {
    AgentTransport agent_transport(host, ectx.log);
    if (agent_transport.hello()) {
      agent_available = true;
      if (ectx.log) {
        ectx.log->info("bot", "Remote agent available; future runs may use persistent channel");
      }
    }
  }

  host.extra_config["agent_available"] = agent_available ? "true" : "false";

  std::cout << "Spawning " << bot_name << " on " << host.label << " (" << host.address << ")...\n";

  RunResult result;
  bool attempted_setup = false;

  while (true) {
    // Create bot instance per attempt (host config may change after setup)
    auto bot = reg.create_bot(bot_name, host, ectx.cfg, ectx.store, ectx.log);

    // Track run in persistence
    int64_t run_id = ectx.store->begin_bot_run(bot_name, host_id);

    result = bot->execute();

    ectx.store->finish_bot_run(run_id, status_to_string(result.status),
                               result.exit_code, result.duration_ms);

    if (!result.json_report.empty()) {
      ectx.store->add_bot_report(run_id, result.json_report);
    }

    ectx.store->update_bot_host_status(host_id, status_to_string(result.status));

    // Handle authentication failures with optional rerun
    if (result.exit_code == 255 &&
        result.stderr_output.find("Permission denied") != std::string::npos &&
        bot_name == "doctor" && !attempted_setup) {

    std::cerr << "\nDoctor Bot could not authenticate to " << host.address << ".\n";
    std::cerr << "Nazg can run 'nazg bot setup --host " << host.address
                << "' to copy keys and enable SSH access.\n";

      prompt::Prompt setup_prompt(ectx.log);
      setup_prompt.title("Doctor Bot Setup")
                  .question("Run 'nazg bot setup' now to configure SSH access?")
                  .info("Host: " + host.address)
                  .action("Command: nazg bot setup --host " + host.address)
                  .style(prompt::Style::VERBOSE)
                  .colors(true);

      if (setup_prompt.confirm()) {
        std::vector<std::string> setup_storage;
        setup_storage.reserve(4);
        setup_storage.push_back(ectx.prog.empty() ? "nazg" : ectx.prog);
        setup_storage.push_back("bot-setup");
        setup_storage.push_back("--host");
        setup_storage.push_back(host.address);

        std::vector<const char*> setup_argv;
        setup_argv.reserve(setup_storage.size());
        for (const auto& arg : setup_storage) {
          setup_argv.push_back(arg.c_str());
        }

        directive::command_context setup_ctx;
        setup_ctx.argc = static_cast<int>(setup_argv.size());
        setup_ctx.argv = setup_argv.data();

        int setup_rc = cmd_bot_setup(setup_ctx, ectx);
        if (setup_rc == 0) {
          std::cout << "\nSSH setup complete. Running doctor bot again...\n";
          attempted_setup = true;
          host = parse_host_config(ctx, ectx, 3);
          host.id = host_id;
          continue; // Retry execution with new credentials
        }

        std::cerr << "\nBot setup did not complete successfully (exit code " << setup_rc << ").\n";
      }
    }

    break; // No retry scheduled
  }

  // Display formatted summary
  std::cout << "\n";
  std::cout << "┌───────────────────────────────────────────────────────\n";
  std::cout << "│ Bot Execution Summary\n";
  std::cout << "├───────────────────────────────────────────────────────\n";
  std::cout << "│ Bot         : " << bot_name << "\n";
  std::cout << "│ Host        : " << host.label << " (" << host.address << ")\n";
  std::cout << "│ Status      : " << status_to_string(result.status) << "\n";
  std::cout << "│ Exit code   : " << result.exit_code << "\n";
  std::cout << "│ Duration    : " << result.duration_ms << "ms\n";
  std::cout << "└───────────────────────────────────────────────────────\n";

  // Show JSON report if available
  if (!result.json_report.empty()) {
    std::cout << "\n📊 Report:\n" << result.json_report << "\n";
  }

  // Show detailed error output for failures
  if (result.exit_code != 0 || result.status == Status::ERROR || result.status == Status::CRITICAL) {
    std::cout << "\n";

    // Show stdout if present
    if (!result.stdout_output.empty()) {
      std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
      std::cout << "📋 Standard Output:\n";
      std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
      std::cout << result.stdout_output;
      if (!result.stdout_output.empty() && result.stdout_output.back() != '\n') {
        std::cout << "\n";
      }
    }

    // Show stderr if present and different from stdout
    if (!result.stderr_output.empty() && result.stderr_output != result.stdout_output) {
      std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
      std::cout << "❌ Error Output:\n";
      std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
      std::cout << result.stderr_output;
      if (!result.stderr_output.empty() && result.stderr_output.back() != '\n') {
        std::cout << "\n";
      }
    }

    // Helpful hints based on exit code
    if (result.exit_code == 127) {
      std::cerr << "\n💡 Hint: Exit code 127 usually means a command was not found on the remote system.\n";
    } else if (result.exit_code == 126) {
      std::cerr << "\n💡 Hint: Exit code 126 usually means a command exists but is not executable.\n";
    } else if (result.exit_code == 255) {
      std::cerr << "\n💡 Hint: Exit code 255 usually means SSH connection failed.\n";
    } else if (result.exit_code != 0 && result.stdout_output.empty() && result.stderr_output.empty()) {
      std::cerr << "\n💡 Hint: The bot script failed but produced no output. Check SSH connectivity and remote shell.\n";
    }
  }

  return result.exit_code == 0 ? 0 : 1;
}

// nazg bot list
static int cmd_bot_list(const directive::command_context& ctx,
                         const directive::context& ectx) {
  (void)ctx;  // Unused

  std::cout << "Recent bot runs:\n\n";

  // Get recent runs for doctor bot (can be extended to list all bots)
  auto runs = ectx.store->recent_bot_runs("doctor", 20);

  if (runs.empty()) {
    std::cout << "No bot runs found.\n";
    return 0;
  }

  std::cout << std::left;
  std::cout << std::setw(5) << "ID"
            << std::setw(12) << "Bot"
            << std::setw(20) << "Host"
            << std::setw(12) << "Status"
            << std::setw(12) << "Exit Code"
            << std::setw(15) << "Duration (ms)" << "\n";
  std::cout << std::string(76, '-') << "\n";

  for (const auto& run : runs) {
    std::cout << std::setw(5) << map_get(run, "id")
              << std::setw(12) << map_get(run, "bot_name")
              << std::setw(20) << map_get(run, "host_label")
              << std::setw(12) << map_get(run, "status")
              << std::setw(12) << map_get(run, "exit_code")
              << std::setw(15) << map_get(run, "duration_ms") << "\n";
  }

  return 0;
}

// nazg bot report <bot-name> --host <target>
static int cmd_bot_report(const directive::command_context& ctx,
                           const directive::context& ectx) {
  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg bot report <bot-name> --host <target>\n";
    return 1;
  }

  std::string bot_name = ctx.argv[2];
  HostConfig host = parse_host_config(ctx, ectx, 3);

  if (host.address.empty()) {
    std::cerr << "Error: --host is required\n";
    return 1;
  }

  // Get host ID
  auto host_id_opt = ectx.store->get_bot_host_id(host.label);
  if (!host_id_opt) {
    std::cerr << "Error: Host '" << host.label << "' not found. Run 'nazg bot hosts' to view labels.\n";
    return 1;
  }

  int64_t host_id = *host_id_opt;

  // Get latest report
  auto report = ectx.store->latest_bot_report(bot_name, host_id);
  if (!report) {
    std::cout << "No report found for " << bot_name << " on " << host.label << "\n";
    return 0;
  }

  std::cout << "Latest " << bot_name << " report for " << host.label << ":\n\n";
  std::cout << *report << "\n";

  return 0;
}

// nazg bot hosts - List registered hosts with status from Nexus
static int cmd_bot_hosts(const directive::command_context& ctx,
                         const directive::context& ectx) {
  (void)ctx;

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  auto hosts = ectx.store->list_bot_hosts();
  if (hosts.empty()) {
    std::cout << "No bot hosts registered yet. Run 'nazg bot spawn <bot> --host <address>' first.\n";
    return 0;
  }

  std::cout << std::left;
  std::cout << std::setw(16) << "Label"
            << std::setw(22) << "Address"
            << std::setw(12) << "Status"
            << "Last Run" << "\n";
  std::cout << std::string(16 + 22 + 12 + 20, '-') << "\n";

  for (const auto &host : hosts) {
    std::string status = map_get(host, "last_status");
    if (status.empty())
      status = "(none)";
    std::cout << std::setw(16) << map_get(host, "label")
              << std::setw(22) << map_get(host, "address")
              << std::setw(12) << status
              << format_timestamp(map_get(host, "last_run_at")) << "\n";
  }

  return 0;
}

// nazg bot history [--bot NAME] [--host LABEL] [--limit N]
static int cmd_bot_history(const directive::command_context& ctx,
                           const directive::context& ectx) {
  std::string bot_filter;
  std::string host_label;
  int limit = 20;

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--bot" && i + 1 < ctx.argc) {
      bot_filter = ctx.argv[++i];
    } else if (arg == "--host" && i + 1 < ctx.argc) {
      host_label = ctx.argv[++i];
    } else if (arg == "--limit" && i + 1 < ctx.argc) {
      try {
        limit = std::stoi(ctx.argv[++i]);
        if (limit <= 0)
          limit = 20;
      } catch (...) {
        std::cerr << "Invalid value for --limit\n";
        return 1;
      }
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nazg bot history [--bot NAME] [--host LABEL] [--limit N]\n";
      return 0;
    }
  }

  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::vector<std::map<std::string, std::string>> runs;
  std::string effective_host_label;

  if (!host_label.empty()) {
    auto host_id = ectx.store->get_bot_host_id(host_label);
    if (!host_id) {
      std::cerr << "Host '" << host_label << "' not found. Run 'nazg bot hosts' to view labels.\n";
      return 1;
    }
    runs = ectx.store->recent_bot_runs_for_host(*host_id, limit);
    effective_host_label = host_label;

    if (!bot_filter.empty() && bot_filter != "all") {
      runs.erase(std::remove_if(runs.begin(), runs.end(), [&](const auto &run) {
                    return map_get(run, "bot_name") != bot_filter;
                  }),
                 runs.end());
    }
  } else {
    if (bot_filter.empty())
      bot_filter = "doctor";
    runs = ectx.store->recent_bot_runs(bot_filter, limit);
  }

  if (runs.empty()) {
    std::cout << "No bot runs found for the specified criteria.\n";
    return 0;
  }

  std::cout << std::left;
  std::cout << std::setw(6) << "ID"
            << std::setw(12) << "Bot"
            << std::setw(16) << "Host"
            << std::setw(12) << "Status"
            << std::setw(8) << "Exit"
            << std::setw(20) << "Started"
            << "Duration" << "\n";
  std::cout << std::string(6 + 12 + 16 + 12 + 8 + 20 + 12, '-') << "\n";

  for (const auto &run : runs) {
    std::string host = effective_host_label.empty()
                           ? map_get(run, "host_label", host_label)
                           : effective_host_label;
    std::cout << std::setw(6) << map_get(run, "id")
              << std::setw(12) << map_get(run, "bot_name")
              << std::setw(16) << host
              << std::setw(12) << map_get(run, "status")
              << std::setw(8) << map_get(run, "exit_code")
              << std::setw(20) << format_timestamp(map_get(run, "started_at"))
              << format_duration_ms(map_get(run, "duration_ms")) << "\n";
  }

  return 0;
}

static bool manage_single_host(HostRecord &host, const directive::context &ectx,
                               const PromptFlags &flags) {
  auto *store = ectx.store;
  if (!store)
    return false;

  while (true) {
    SshConfigFields cfg = parse_ssh_config_json(host.ssh_config);

    prompt::Prompt prompt(ectx.log);
    prompt.title("bot-manage")
          .fact("Label", host.label.empty() ? "(unnamed)" : host.label)
          .fact("Address", host.address.empty() ? "(none)" : host.address)
          .fact("SSH key", cfg.key.empty() ? "(default)" : cfg.key)
          .fact("Port", std::to_string(cfg.port))
          .fact("Agent port", std::to_string(cfg.agent_port))
          .fact("Last status", host.last_status.empty() ? "(none)" : host.last_status)
          .fact("Last run", format_timestamp(host.last_run_at))
          .question("Choose an action for this host")
          .style(flags.style)
          .force_yes(flags.force_yes)
          .force_no(flags.force_no);

    std::vector<std::string> options = {
        "Edit label",
        "Edit address",
        "Edit SSH key / ports",
        "Delete host",
        "Back"};

    int choice = prompt.choice(options, static_cast<int>(options.size()) - 1);
    switch (choice) {
    case 0: {
      prompt::Prompt rename_prompt(ectx.log);
      rename_prompt.title("bot-manage")
                  .question("Enter new label (leave blank to keep current)")
                  .style(flags.style)
                  .force_yes(flags.force_yes)
                  .force_no(flags.force_no);
      std::string new_label = rename_prompt.input(host.label);
      if (new_label.empty() || new_label == host.label) {
        if (new_label.empty())
          std::cout << "Label unchanged.\n";
        break;
      }
      if (!store->update_bot_host(host.id, new_label, host.address, host.ssh_config)) {
        std::cerr << "Failed to update host label.\n";
      } else {
        host.label = new_label;
        std::cout << "✓ Updated host label to '" << host.label << "'\n";
      }
      break;
    }
    case 1: {
      prompt::Prompt addr_prompt(ectx.log);
      addr_prompt.title("bot-manage")
                 .question("Enter new address (user@host or host)")
                 .style(flags.style)
                 .force_yes(flags.force_yes)
                 .force_no(flags.force_no);
      std::string new_address = addr_prompt.input(host.address);
      if (new_address.empty() || new_address == host.address) {
        if (new_address.empty())
          std::cout << "Address unchanged.\n";
        break;
      }
      if (!store->update_bot_host(host.id, host.label, new_address, host.ssh_config)) {
        std::cerr << "Failed to update host address.\n";
      } else {
        host.address = new_address;
        std::cout << "✓ Updated address to '" << host.address << "'\n";
      }
      break;
    }
    case 2: {
      prompt::Prompt key_prompt(ectx.log);
      key_prompt.title("bot-manage")
                .question("Enter new SSH key path (leave blank to keep current)")
                .style(flags.style)
                .force_yes(flags.force_yes)
                .force_no(flags.force_no);
      std::string new_key = key_prompt.input(cfg.key);
      if (!new_key.empty())
        cfg.key = new_key;

      prompt::Prompt port_prompt(ectx.log);
      port_prompt.title("bot-manage")
                 .question("Enter SSH port (leave blank to keep current)")
                 .style(flags.style)
                 .force_yes(flags.force_yes)
                 .force_no(flags.force_no);
      std::string port_str = port_prompt.input(std::to_string(cfg.port));
      if (!port_str.empty()) {
        try {
          int port = std::stoi(port_str);
          if (port > 0 && port < 65536)
            cfg.port = port;
          else
            std::cerr << "Invalid port; keeping previous value.\n";
        } catch (...) {
          std::cerr << "Invalid port; keeping previous value.\n";
        }
      }

      prompt::Prompt agent_prompt(ectx.log);
      agent_prompt.title("bot-manage")
                  .question("Enter agent port (leave blank to keep current)")
                  .style(flags.style)
                  .force_yes(flags.force_yes)
                  .force_no(flags.force_no);
      std::string agent_str = agent_prompt.input(std::to_string(cfg.agent_port));
      if (!agent_str.empty()) {
        try {
          int agent_port = std::stoi(agent_str);
          if (agent_port > 0 && agent_port < 65536)
            cfg.agent_port = agent_port;
          else
            std::cerr << "Invalid agent port; keeping previous value.\n";
        } catch (...) {
          std::cerr << "Invalid agent port; keeping previous value.\n";
        }
      }

      std::string new_config = build_ssh_config_json(cfg);
      if (!store->update_bot_host(host.id, host.label, host.address, new_config)) {
        std::cerr << "Failed to update SSH settings.\n";
      } else {
        host.ssh_config = new_config;
        std::cout << "✓ Updated SSH configuration.\n";
      }
      break;
    }
    case 3: {
      prompt::Prompt confirm_prompt(ectx.log);
      confirm_prompt.title("bot-manage")
                    .question("Delete this host and all associated bot history?")
                    .style(flags.style)
                    .force_yes(flags.force_yes)
                    .force_no(flags.force_no)
                    .warning("This action removes stored runs and reports for the host.");
      if (confirm_prompt.confirm(false)) {
        if (!store->delete_bot_host(host.id)) {
          std::cerr << "Failed to delete host.\n";
        } else {
          std::cout << "✓ Deleted host '" << host.label << "'\n";
          return true;
        }
      }
      break;
    }
    default:
      return false;
    }
  }
}

static int cmd_bot_manage(const directive::command_context& ctx,
                          const directive::context& ectx) {
  if (!ectx.store) {
    std::cerr << "Error: Database not initialized\n";
    return 1;
  }

  std::string target_label;
  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--host" && i + 1 < ctx.argc) {
      target_label = ctx.argv[++i];
    }
  }

  auto flags = parse_prompt_flags(ctx, ectx, 2);

  auto rows = ectx.store->list_bot_hosts();
  if (rows.empty()) {
    std::cout << "No bot hosts registered yet. Run 'nazg bot spawn <bot> --host <address>' first.\n";
    return 0;
  }

  std::vector<HostRecord> hosts;
  hosts.reserve(rows.size());
  for (const auto &row : rows) {
    hosts.push_back(host_from_row(row));
  }

  if (!target_label.empty()) {
    auto it = std::find_if(hosts.begin(), hosts.end(), [&](const HostRecord &h) {
      return h.label == target_label;
    });
    if (it == hosts.end()) {
      std::cerr << "Host '" << target_label << "' not found. Run 'nazg bot hosts' to view labels.\n";
      return 1;
    }
    manage_single_host(*it, ectx, flags);
    return 0;
  }

  while (!hosts.empty()) {
    prompt::Prompt prompt(ectx.log);
    prompt.title("bot-manage")
          .question("Select a host to manage")
          .style(flags.style)
          .force_yes(flags.force_yes)
          .force_no(flags.force_no);

    std::vector<std::string> options;
    options.reserve(hosts.size() + 1);
    for (const auto &host : hosts) {
      std::ostringstream line;
      line << host.label;
      if (!host.address.empty())
        line << " (" << host.address << ")";
      if (!host.last_status.empty())
        line << " — " << host.last_status;
      options.push_back(line.str());
    }
    options.push_back("Exit");

    int choice = prompt.choice(options, static_cast<int>(options.size()) - 1);
    if (choice < 0 || choice >= static_cast<int>(options.size()) - 1)
      break;

    bool removed = manage_single_host(hosts[choice], ectx, flags);
    if (removed) {
      hosts.erase(hosts.begin() + choice);
      if (hosts.empty()) {
        std::cout << "No bot hosts remain.\n";
        break;
      }
    }
  }

  return 0;
}

static int cmd_bot_root(const directive::command_context& ctx,
                        const directive::context& ectx) {
  if (ctx.argc < 3) {
    print_bot_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
    return 0;
  }

  std::string sub = to_lower_copy(ctx.argv[2]);
  if (sub == "--help" || sub == "-h" || sub == "help") {
    print_bot_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
    return 0;
  }

  if (sub == "spawn") {
    return forward_bot_command(ctx, ectx, cmd_bot_spawn, "bot-spawn", 3);
  }
  if (sub == "doctor") {
    return forward_bot_command(ctx, ectx, cmd_bot_spawn, "bot-spawn", 3, {"doctor"});
  }
  if (sub == "git-doctor") {
    return forward_bot_command(ctx, ectx, cmd_bot_spawn, "bot-spawn", 3, {"git-doctor"});
  }
  if (sub == "setup") {
    return forward_bot_command(ctx, ectx, cmd_bot_setup, "bot-setup", 3);
  }
  if (sub == "hosts") {
    return forward_bot_command(ctx, ectx, cmd_bot_hosts, "bot-hosts", 3);
  }
  if (sub == "history") {
    return forward_bot_command(ctx, ectx, cmd_bot_history, "bot-history", 3);
  }
  if (sub == "manage") {
    return forward_bot_command(ctx, ectx, cmd_bot_manage, "bot-manage", 3);
  }
  if (sub == "list") {
    return forward_bot_command(ctx, ectx, cmd_bot_list, "bot-list", 3);
  }
  if (sub == "report") {
    return forward_bot_command(ctx, ectx, cmd_bot_report, "bot-report", 3);
  }

  std::cerr << "Unknown bot subcommand: " << sub << "\n\n";
  print_bot_help(ctx.argc > 0 ? ctx.argv[0] : "nazg");
  return 2;
}

// nazg bot setup --host <target> [options]
static int cmd_bot_setup(const directive::command_context& ctx,
                          const directive::context& ectx) {
  if (ctx.argc < 3) {
    std::cerr << "Usage: nazg bot setup --host <target> [options]\n";
    std::cerr << "Set up SSH key authentication for bot access to remote host\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  --host <address>      SSH address (user@host or host) [required]\n";
    std::cerr << "  --label <name>        Friendly name for host (defaults to address)\n";
    std::cerr << "  --password <pass>     Password (prompts if not provided)\n";
    std::cerr << "  --key <path>          Use existing SSH key (default: ~/.ssh/id_ed25519)\n";
    std::cerr << "  --generate-key        Force generate new SSH key\n";
    std::cerr << "  --port <number>       SSH port (default: 22)\n";
    std::cerr << "  --agent-port <number> Remote agent port (default: 7070)\n";
    return 1;
  }

  // Parse arguments
  HostConfig host;
  std::string password;
  std::string ssh_key_path;
  bool generate_key = false;

  for (int i = 2; i < ctx.argc; ++i) {
    std::string arg = ctx.argv[i];
    if (arg == "--host" && i + 1 < ctx.argc) {
      host.address = ctx.argv[++i];
    } else if (arg == "--label" && i + 1 < ctx.argc) {
      host.label = ctx.argv[++i];
    } else if (arg == "--password" && i + 1 < ctx.argc) {
      password = ctx.argv[++i];
    } else if (arg == "--key" && i + 1 < ctx.argc) {
      ssh_key_path = ctx.argv[++i];
    } else if (arg == "--generate-key") {
      generate_key = true;
    } else if (arg == "--port" && i + 1 < ctx.argc) {
      host.ssh_port = std::stoi(ctx.argv[++i]);
    }
  }

  if (host.address.empty()) {
    std::cerr << "Error: --host is required\n";
    return 1;
  }

  if (host.label.empty()) {
    host.label = host.address;
  }

  std::cout << "Setting up bot access to " << host.label << " (" << host.address << ")...\n\n";

  // Step 1: Check/generate SSH key
  if (ssh_key_path.empty()) {
    const char *home = std::getenv("HOME");
    if (!home) {
      std::cerr << "Error: HOME environment variable is not set\n";
      return 1;
    }
    ssh_key_path = std::string(home) + "/.ssh/id_ed25519";
  }

  namespace fs = std::filesystem;
  if (!fs::exists(ssh_key_path) || generate_key) {
    std::cout << "Generating SSH key at " << ssh_key_path << "...\n";
    std::string keygen_cmd = "ssh-keygen -t ed25519 -f " +
                             ::nazg::system::shell_quote(ssh_key_path) +
                             " -N \"\" -q";
    int rc = ::nazg::system::run_command(keygen_cmd);
    if (rc != 0) {
      std::cerr << "Error: Failed to generate SSH key\n";
      return 1;
    }
    std::cout << "✓ SSH key generated\n";
  } else {
    std::cout << "✓ Using existing SSH key: " << ssh_key_path << "\n";
  }

  host.ssh_key = ssh_key_path;

  // Step 2: Ensure sshpass is available for password-based key copy
  if (!::nazg::system::is_package_installed("sshpass")) {
    prompt::Prompt pkg_prompt(ectx.log);
    if (!::nazg::system::install_package("sshpass", &pkg_prompt, ectx.log)) {
      std::cerr << "\nError: sshpass is required for automated SSH setup.\n";
      return 1;
    }
  }

  // Step 3: Prompt for password if not provided
  if (password.empty()) {
    password = read_password("Enter password for " + host.address);
    if (password.empty()) {
      std::cerr << "Error: Password required\n";
      return 1;
    }
  }

  // Step 4: Copy SSH key using sshpass
  std::cout << "\nCopying SSH key to " << host.address << "...\n";

  std::ostringstream ssh_copy_cmd;
  std::ostringstream ssh_copy_display;
  // Use SSHPASS env var instead of -p to keep password out of process listings
  ssh_copy_cmd << "SSHPASS=" << ::nazg::system::shell_quote(password)
               << " sshpass -e ssh-copy-id -o StrictHostKeyChecking=no";
  ssh_copy_display << "SSHPASS='***' sshpass -e ssh-copy-id -o StrictHostKeyChecking=no";

  ssh_copy_cmd << " -i " << ::nazg::system::shell_quote(ssh_key_path);
  ssh_copy_display << " -i " << ::nazg::system::shell_quote(ssh_key_path);

  if (host.ssh_port != 22) {
    ssh_copy_cmd << " -p " << host.ssh_port;
    ssh_copy_display << " -p " << host.ssh_port;
  }

  ssh_copy_cmd << " " << ::nazg::system::shell_quote(host.address) << " 2>&1";
  ssh_copy_display << " " << ::nazg::system::shell_quote(host.address);

  auto copy_result = ::nazg::system::run_command_capture(ssh_copy_cmd.str());

  if (copy_result.exit_code != 0) {
    std::cerr << "Error: Failed to copy SSH key\n";
    if (!copy_result.output.empty()) {
      std::cerr << "Output: " << copy_result.output << "\n";
    }

    std::cerr << "Command (password redacted): " << ssh_copy_display.str() << "\n";

    auto hint = diagnose_ssh_failure(host, copy_result.output, copy_result.exit_code);
    if (!hint.summary.empty()) {
      std::cerr << "\nDiagnosis: " << hint.summary << "\n";
      for (const auto &tip : hint.tips) {
        std::cerr << "  - " << tip << "\n";
      }
    }
    return 1;
  }

  std::cout << "✓ SSH key copied successfully\n";

  // Step 5: Test connection
  std::cout << "\nTesting connection...\n";

  std::ostringstream test_cmd;
  test_cmd << "ssh -o BatchMode=yes -o ConnectTimeout=5";

  if (host.ssh_port != 22) {
    test_cmd << " -p " << host.ssh_port;
  }

  test_cmd << " -i " << ::nazg::system::shell_quote(ssh_key_path)
           << " " << ::nazg::system::shell_quote(host.address)
           << " 'echo OK' 2>&1";
  std::ostringstream test_display;
  test_display << "ssh -o BatchMode=yes -o ConnectTimeout=5";
  if (host.ssh_port != 22) {
    test_display << " -p " << host.ssh_port;
  }
  test_display << " -i " << ::nazg::system::shell_quote(ssh_key_path)
               << " " << ::nazg::system::shell_quote(host.address)
               << " 'echo OK'";

  auto test_result = ::nazg::system::run_command_capture(test_cmd.str());

  if (test_result.exit_code != 0) {
    std::cerr << "Error: Connection test failed\n";
    if (!test_result.output.empty()) {
      std::cerr << "Output: " << test_result.output << "\n";
    }

    std::cerr << "Command: " << test_display.str() << "\n";

    auto hint = diagnose_ssh_failure(host, test_result.output, test_result.exit_code);
    if (!hint.summary.empty()) {
      std::cerr << "\nDiagnosis: " << hint.summary << "\n";
      for (const auto &tip : hint.tips) {
        std::cerr << "  - " << tip << "\n";
      }
    }
    return 1;
  }

  std::cout << "✓ Connection test successful\n";

  // Step 6: Save host configuration to Nexus
  std::ostringstream ssh_config;
  ssh_config << "{\"key\":\"" << ssh_key_path << "\",\"port\":" << host.ssh_port << "}";

  if (auto existing_id = ectx.store->get_bot_host_id(host.label)) {
    (void)*existing_id;
    std::cout << "\n✓ Updated existing host configuration\n";
  } else {
    ectx.store->add_bot_host(host.label, host.address, ssh_config.str());
    std::cout << "\n✓ Host configuration saved\n";
  }

  // Success message
  std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
  std::cout << "✓ Successfully set up bot access!\n\n";
  std::cout << "Host:    " << host.label << "\n";
  std::cout << "Address: " << host.address << "\n";
  std::cout << "SSH Key: " << ssh_key_path << "\n";
  std::cout << "\nYou can now run:\n";
  std::cout << "  nazg bot doctor --host " << host.label << "\n";
  std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

  return 0;
}

void register_commands(directive::registry& reg, const directive::context& ctx) {
  (void)ctx;  // Unused for now

  directive::command_spec spec{};
  spec.name = "bot";
  spec.summary = "Bot toolkit and utilities (run 'nazg bot --help' for details)";
  spec.long_help =
      "Subcommands:\n"
      "  spawn <bot>    Run a bot by name (e.g., doctor, git-doctor)\n"
      "  doctor        Shortcut for 'spawn doctor'\n"
      "  git-doctor    Shortcut for 'spawn git-doctor'\n"
      "  setup         Configure SSH access for a host\n"
      "  hosts         List registered bot hosts\n"
      "  history       Show historical bot runs\n"
      "  list          Summarise recent bot runs\n"
      "  report        Show latest report for a host\n"
      "  manage        Interactive host manager\n";
  spec.run = cmd_bot_root;
  reg.add(spec);
}

} // namespace nazg::bot

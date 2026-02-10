#include "tui/managers/command_manager.hpp"
#include <sstream>

namespace nazg::tui {

CommandManager::CommandManager() = default;

bool CommandManager::register_command(const Command& cmd) {
  if (cmd.name.empty() || !cmd.func) {
    return false;
  }
  commands_[cmd.name] = cmd;
  return true;
}

bool CommandManager::register_command(const std::string& name,
                                      const std::string& description,
                                      CommandFunc func) {
  return register_command(Command{name, description, name, std::move(func)});
}

bool CommandManager::unregister_command(const std::string& name) {
  return commands_.erase(name) > 0;
}

bool CommandManager::execute(TUIContext& ctx, const std::string& name,
                             const std::vector<std::string>& args) {
  auto it = commands_.find(name);
  if (it == commands_.end()) {
    return false;
  }

  try {
    return it->second.func(ctx, args);
  } catch (...) {
    return false;
  }
}

bool CommandManager::execute_line(TUIContext& ctx, const std::string& cmdline) {
  auto [name, args] = parse_cmdline(cmdline);
  if (name.empty()) {
    return false;
  }
  return execute(ctx, name, args);
}

bool CommandManager::has_command(const std::string& name) const {
  return commands_.find(name) != commands_.end();
}

std::optional<Command> CommandManager::get_command(const std::string& name) const {
  auto it = commands_.find(name);
  if (it == commands_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<Command> CommandManager::get_all_commands() const {
  std::vector<Command> result;
  result.reserve(commands_.size());
  for (const auto& [name, cmd] : commands_) {
    result.push_back(cmd);
  }
  return result;
}

std::string CommandManager::help_text() const {
  std::ostringstream oss;
  oss << "=== Available Commands ===\n\n";

  for (const auto& [name, cmd] : commands_) {
    oss << "  " << cmd.usage << "\n";
    oss << "    " << cmd.description << "\n\n";
  }

  return oss.str();
}

std::pair<std::string, std::vector<std::string>>
CommandManager::parse_cmdline(const std::string& cmdline) {
  std::istringstream iss(cmdline);
  std::string command;
  iss >> command;

  std::vector<std::string> args;
  std::string arg;
  while (iss >> arg) {
    args.push_back(arg);
  }

  return {command, args};
}

void CommandManager::register_builtins(TUIContext& ctx) {
  // The actual implementation is in tui_context.cpp where TUIContext is fully defined
  // This avoids the issue of using an incomplete type in lambda functions
  // The real work happens in TUIContext::initialize()
}

} // namespace nazg::tui

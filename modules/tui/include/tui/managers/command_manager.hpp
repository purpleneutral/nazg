#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nazg::tui {

// Forward declaration - TUIContext is defined in tui_context.hpp
// Don't include tui_context.hpp here to avoid circular dependency
class TUIContext;

/**
 * @brief Command function signature
 *
 * Commands receive:
 * - TUIContext& - access to all managers and state
 * - std::vector<std::string> - command arguments
 *
 * Commands return:
 * - bool - true if command succeeded
 */
using CommandFunc = std::function<bool(TUIContext&, const std::vector<std::string>&)>;

/**
 * @brief Command definition
 */
struct Command {
  std::string name;
  std::string description;
  std::string usage;  // e.g., "split-horizontal [shell]"
  CommandFunc func;

  Command() = default;
  Command(const std::string& n, const std::string& desc,
          const std::string& use, CommandFunc f)
      : name(n), description(desc), usage(use), func(std::move(f)) {}
};

/**
 * @brief CommandManager handles command registration and execution
 *
 * This manager provides:
 * - Command registration API
 * - Command execution
 * - Built-in commands
 * - External module API for registering commands
 */
class CommandManager {
public:
  CommandManager();

  /**
   * @brief Register a command
   * @param cmd Command to register
   * @return true if registered successfully
   */
  bool register_command(const Command& cmd);

  /**
   * @brief Register a command with just name and function
   */
  bool register_command(const std::string& name, const std::string& description,
                       CommandFunc func);

  /**
   * @brief Unregister a command
   * @param name Command name
   * @return true if unregistered successfully
   */
  bool unregister_command(const std::string& name);

  /**
   * @brief Execute a command by name
   * @param ctx TUI context
   * @param name Command name
   * @param args Command arguments
   * @return true if command executed successfully
   */
  bool execute(TUIContext& ctx, const std::string& name,
               const std::vector<std::string>& args = {});

  /**
   * @brief Execute a command line string (parses command and args)
   * @param ctx TUI context
   * @param cmdline Command line (e.g., "split-horizontal /bin/bash")
   * @return true if command executed successfully
   */
  bool execute_line(TUIContext& ctx, const std::string& cmdline);

  /**
   * @brief Check if command exists
   */
  bool has_command(const std::string& name) const;

  /**
   * @brief Get command info
   */
  std::optional<Command> get_command(const std::string& name) const;

  /**
   * @brief Get all registered commands
   */
  std::vector<Command> get_all_commands() const;

  /**
   * @brief Register all built-in commands
   */
  void register_builtins(TUIContext& ctx);

  /**
   * @brief Get help text for all commands
   */
  std::string help_text() const;

private:
  std::map<std::string, Command> commands_;

  /**
   * @brief Parse command line into command name and args
   */
  static std::pair<std::string, std::vector<std::string>>
  parse_cmdline(const std::string& cmdline);
};

} // namespace nazg::tui

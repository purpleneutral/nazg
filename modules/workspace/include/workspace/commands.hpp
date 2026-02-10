#pragma once
#include <string>
#include <vector>

namespace nazg::directive {
class registry;
struct context;
} // namespace nazg::directive

namespace nazg::workspace {

// Register all workspace commands
void register_commands(nazg::directive::registry &reg,
                       nazg::directive::context &ctx);

// Command handlers
int cmd_workspace_snapshot(const std::vector<std::string> &args,
                           nazg::directive::context &ctx);
int cmd_workspace_history(const std::vector<std::string> &args,
                          nazg::directive::context &ctx);
int cmd_workspace_show(const std::vector<std::string> &args,
                       nazg::directive::context &ctx);
int cmd_workspace_diff(const std::vector<std::string> &args,
                       nazg::directive::context &ctx);
int cmd_workspace_restore(const std::vector<std::string> &args,
                          nazg::directive::context &ctx);
int cmd_workspace_tag(const std::vector<std::string> &args,
                      nazg::directive::context &ctx);
int cmd_workspace_prune(const std::vector<std::string> &args,
                        nazg::directive::context &ctx);

} // namespace nazg::workspace

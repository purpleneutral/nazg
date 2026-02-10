#pragma once
#include "bot/registry.hpp"
#include "bot/transport.hpp"
#include <memory>

namespace nazg {
namespace config { class store; }
namespace nexus { class Store; }
namespace blackbox { class logger; }
}

namespace nazg::bot {

// Git Doctor Bot - Git server infrastructure health diagnostics
class GitDoctorBot : public BotBase {
public:
  GitDoctorBot(const HostConfig& host,
               ::nazg::config::store* cfg,
               ::nazg::nexus::Store* store,
               ::nazg::blackbox::logger* log);

  void initialize_transports();

  ~GitDoctorBot() override = default;

  // Execute git server health check on remote host
  RunResult execute() override;

  std::string name() const override { return "git-doctor"; }

private:
  std::unique_ptr<SSHTransport> ssh_transport_;
  std::unique_ptr<AgentTransport> agent_transport_;
  bool prefer_agent_ = false;

  // Git server configuration
  std::string git_server_type_ = "cgit";
  std::string repo_base_path_ = "/srv/git";
  std::string config_path_ = "/etc/cgitrc";
  std::string git_server_label_;

  // Get git doctor script content
  std::string get_script_content() const;

  // Parse JSON report from bot output
  ReportData parse_report(const std::string& json_output) const;
};

} // namespace nazg::bot

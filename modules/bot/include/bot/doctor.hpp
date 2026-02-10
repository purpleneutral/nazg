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

// Doctor Bot - System health diagnostics
class DoctorBot : public BotBase {
public:
  DoctorBot(const HostConfig& host,
            ::nazg::config::store* cfg,
            ::nazg::nexus::Store* store,
            ::nazg::blackbox::logger* log)
      : BotBase(host, cfg, store, log),
        ssh_transport_(std::make_unique<SSHTransport>(host, log)) {
    auto it = host.extra_config.find("agent_available");
    if (it != host.extra_config.end() && it->second == "true") {
      agent_transport_ = std::make_unique<AgentTransport>(host, log);
      prefer_agent_ = true;
    }
  }

  ~DoctorBot() override = default;

  // Execute health check on remote host
  RunResult execute() override;

  std::string name() const override { return "doctor"; }

private:
  std::unique_ptr<SSHTransport> ssh_transport_;
  std::unique_ptr<AgentTransport> agent_transport_;
  bool prefer_agent_ = false;

  // Get doctor script content
  std::string get_script_content() const;

  // Parse JSON report from bot output
  ReportData parse_report(const std::string& json_output) const;
};

} // namespace nazg::bot

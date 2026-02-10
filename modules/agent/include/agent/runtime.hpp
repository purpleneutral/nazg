#pragma once

#include "agent/protocol.hpp"
#include "agent/docker_scanner.hpp"
#include "agent/local_store.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace nazg {
namespace blackbox {
class logger;
} // namespace blackbox
namespace agent {

struct Options {
  std::string bind_address = "127.0.0.1";
  std::uint16_t port = 7070;
  bool verbose = false;
  std::string server_label = "unknown";
  std::string db_path = "/var/lib/nazg-agent/agent.db";
  int scan_interval_seconds = 60;  // Scan every 60 seconds
  bool enable_docker_monitoring = true;
};

class Runtime {
public:
  Runtime(const Options &opts, ::nazg::blackbox::logger *log = nullptr);
  ~Runtime();

  // Starts the listener thread and scanner thread. Returns false if the listener could not bind.
  bool start();
  void stop();

  bool is_running() const { return running_.load(); }
  std::uint16_t port() const { return port_in_use_; }

private:
  bool bind_socket();
  bool init_database();
  void accept_loop();
  void scanner_loop();
  void handle_client(int client_fd);
  bool read_message(int fd, ::nazg::agent::protocol::Header &header,
                    std::string &payload);
  std::pair<int, std::string> run_script(const std::string &script);
  void perform_docker_scan();
  std::string build_registration_response();

  Options opts_;
  ::nazg::blackbox::logger *log_ = nullptr;

  int listen_fd_ = -1;
  std::uint16_t port_in_use_ = 0;
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> thread_;
  std::unique_ptr<std::thread> scanner_thread_;

  std::unique_ptr<DockerScanner> scanner_;
  std::unique_ptr<LocalStore> store_;
};

} // namespace agent
} // namespace nazg

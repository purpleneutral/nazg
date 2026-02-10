#pragma once

#include "agent/docker_scanner.hpp"
#include "nexus/connection.hpp"
#include <memory>
#include <string>
#include <vector>

namespace nazg::blackbox {
class logger;
}

namespace nazg::agent {

// Local SQLite database for caching Docker scan results
class LocalStore {
public:
  static std::unique_ptr<LocalStore> create(const std::string &db_path,
                                            ::nazg::blackbox::logger *log = nullptr);

  explicit LocalStore(std::unique_ptr<::nazg::nexus::Connection> conn,
                     ::nazg::blackbox::logger *log = nullptr);
  ~LocalStore();

  // Initialize database with migrations
  bool initialize();

  // Configuration
  void set_server_label(const std::string &label);
  std::string get_server_label();
  void set_control_center_url(const std::string &url);
  std::string get_control_center_url();
  void update_last_scan_time(int64_t timestamp);
  int64_t get_last_scan_time();

  // Store scan results
  void store_containers(const std::vector<ContainerInfo> &containers, int64_t scan_time);
  void store_compose_files(const std::vector<ComposeFileInfo> &files, int64_t scan_time);
  void store_images(const std::vector<DockerImageInfo> &images, int64_t scan_time);
  void store_networks(const std::vector<DockerNetworkInfo> &networks, int64_t scan_time);
  void store_volumes(const std::vector<DockerVolumeInfo> &volumes, int64_t scan_time);

  // Cleanup old data
  void cleanup_old_data(int64_t older_than_timestamp);

  // Record scan
  void record_scan(int64_t timestamp, int containers, int images,
                   int networks, int volumes, int compose_files);
  void mark_scan_sent(int64_t scan_id);

  // Get latest cached data
  std::vector<ContainerInfo> get_cached_containers();
  std::vector<ComposeFileInfo> get_cached_compose_files();
  std::vector<DockerImageInfo> get_cached_images();
  std::vector<DockerNetworkInfo> get_cached_networks();
  std::vector<DockerVolumeInfo> get_cached_volumes();

private:
  std::unique_ptr<::nazg::nexus::Connection> conn_;
  ::nazg::blackbox::logger *log_ = nullptr;

  bool run_migrations();
  void set_config(const std::string &key, const std::string &value);
  std::string get_config(const std::string &key, const std::string &default_val = "");
};

} // namespace nazg::agent

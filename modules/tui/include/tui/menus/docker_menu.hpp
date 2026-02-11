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

#pragma once

#include "tui/menu.hpp"
#include "tui/tui_context.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <string_view>

namespace nazg::nexus {
class Store;
}

namespace nazg::docker_monitor {
class Orchestrator;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::tui {

/**
 * @brief Docker management dashboard menu
 *
 * Provides a Portainer-like interface for managing Docker containers
 * across multiple servers. Features:
 * - Server selector dropdown
 * - Container list with status
 * - Stack management
 * - Interactive controls (start, stop, restart, logs)
 */
class DockerMenu : public Menu {
public:
  explicit DockerMenu(nazg::nexus::Store* store,
                      nazg::docker_monitor::Orchestrator* orchestrator = nullptr,
                      nazg::blackbox::logger* log = nullptr);
  ~DockerMenu() override = default;

  // Menu interface
  std::string id() const override { return "docker"; }
  std::string title() const override { return "Docker Dashboard"; }
  bool supports_insert_mode() const override { return true; }
  void build(TUIContext& ctx) override;
  void on_load() override;
  void on_unload() override;
  void on_resume() override;

  // State preservation
  MenuState save_state() const override;
  void restore_state(const MenuState& state) override;

private:
  // Data structures
  struct ServerInfo {
    int64_t id;
    std::string label;
    std::string host;
    std::string status;
    int container_count;
  };

  struct ContainerInfo {
    std::string id;
    std::string name;
    std::string image;
    std::string state;
    std::string status;
    std::string health_status;
    std::string service_name;
  };

  struct StackInfo {
    int64_t id;
    std::string name;
    std::string description;
    int service_count;
    int priority;
  };

  // Data loading
  void load_servers();
  void load_containers(int64_t server_id);
  void load_stacks(int64_t server_id);

  // Event handlers
  void on_server_selected(int index);
  void on_container_selected(int index);
  void on_stack_selected(int index);
  void on_start_container();
  void on_stop_container();
  void on_restart_container();
  void on_view_logs();
  void on_refresh();

  // UI helpers
  std::string format_container_status(const ContainerInfo& container) const;
  std::string get_status_color(const std::string& state) const;
  void render_containers_tab(std::vector<ftxui::Element>& elements);
  void render_images_tab(std::vector<ftxui::Element>& elements);
  void render_volumes_tab(std::vector<ftxui::Element>& elements);
  void render_networks_tab(std::vector<ftxui::Element>& elements);
  void render_stacks_tab(std::vector<ftxui::Element>& elements);

  // Tab management
  enum class Tab {
    CONTAINERS,
    IMAGES,
    VOLUMES,
    NETWORKS,
    STACKS
  };

  struct ImageInfo {
    std::string id;
    std::string repository;
    std::string tag;
    std::string size;
    std::string created;
  };

  struct VolumeInfo {
    std::string name;
    std::string driver;
    std::string mountpoint;
  };

  struct NetworkInfo {
    std::string id;
    std::string name;
    std::string driver;
    std::string scope;
  };

  void load_images(int64_t server_id);
  void load_volumes(int64_t server_id);
  void load_networks(int64_t server_id);
  void switch_tab(Tab tab);
  void cycle_tab(int direction);
  std::string tab_label(Tab tab) const;
  void register_commands();
  void register_key_bindings();
  void unregister_key_bindings();
  bool on_run_hello_world();
  void set_status(const std::string& message);
  void set_error(const std::string& message);
  std::string first_line(std::string_view text) const;

  // State
  nazg::nexus::Store* store_;
  nazg::docker_monitor::Orchestrator* orchestrator_;
  nazg::blackbox::logger* log_;
  TUIContext* ctx_ = nullptr;

  // Data
  std::vector<ServerInfo> servers_;
  std::vector<ContainerInfo> containers_;
  std::vector<StackInfo> stacks_;
  std::vector<ImageInfo> images_;
  std::vector<VolumeInfo> volumes_;
  std::vector<NetworkInfo> networks_;

  std::vector<KeyBinding> active_key_bindings_;
  bool key_bindings_active_ = false;
  static bool commands_registered_;

  // Selection state
  int selected_server_index_ = 0;
  int selected_container_index_ = 0;
  int selected_stack_index_ = 0;
  int selected_image_index_ = 0;
  int selected_volume_index_ = 0;
  int selected_network_index_ = 0;
  int64_t current_server_id_ = 0;
  Tab current_tab_ = Tab::CONTAINERS;

  // Filter state
  std::string container_filter_;
  bool show_stopped_containers_ = true;

  // Loading state
  bool loading_containers_ = false;
  bool loading_stacks_ = false;
  bool loading_images_ = false;
  bool loading_volumes_ = false;
  bool loading_networks_ = false;
  std::string error_message_;
  std::string status_message_;
};

} // namespace nazg::tui

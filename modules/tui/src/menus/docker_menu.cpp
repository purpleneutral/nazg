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

#include "tui/menus/docker_menu.hpp"
#include "tui/ftxui_component.hpp"
#include "tui/layout_utils.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include "docker_monitor/orchestrator.hpp"
#include "docker_monitor/commands.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <algorithm>
#include <sstream>
#include <array>
#include <string_view>

using namespace ftxui;

namespace nazg::tui {

bool DockerMenu::commands_registered_ = false;

DockerMenu::DockerMenu(nazg::nexus::Store* store,
                       nazg::docker_monitor::Orchestrator* orchestrator,
                       nazg::blackbox::logger* log)
    : store_(store), orchestrator_(orchestrator), log_(log) {}

void DockerMenu::build(TUIContext& ctx) {
  ctx_ = &ctx;

  // Create render function that captures this
  auto render_fn = [this]() -> Element {
    std::vector<Element> elements;

    // Title bar
    elements.push_back(text("🐳 Docker Dashboard") | bold | center | bgcolor(Color::Blue));
    elements.push_back(separator());

    // Tab bar
    std::vector<Element> tabs;
    auto make_tab = [&](const std::string& name, Tab tab) {
      bool active = (current_tab_ == tab);
      auto elem = text(" " + name + " ");
      if (active) {
        return elem | bold | bgcolor(Color::Cyan) | color(Color::Black);
      } else {
        return elem | dim;
      }
    };

    tabs.push_back(make_tab("Containers", Tab::CONTAINERS));
    tabs.push_back(text(" "));
    tabs.push_back(make_tab("Images", Tab::IMAGES));
    tabs.push_back(text(" "));
    tabs.push_back(make_tab("Volumes", Tab::VOLUMES));
    tabs.push_back(text(" "));
    tabs.push_back(make_tab("Networks", Tab::NETWORKS));
    tabs.push_back(text(" "));
    tabs.push_back(make_tab("Stacks", Tab::STACKS));

    elements.push_back(hbox(tabs));
    elements.push_back(separator());

    const int wrap_width = ctx_ && ctx_->screen().dimx() > 0
        ? std::max(20, ctx_->screen().dimx() - horizontal_overhead(default_menu_frame_options()))
        : 80;

    // Status messages
    if (!error_message_.empty()) {
      auto block = make_wrapped_block("❌ " + error_message_, wrap_width) |
                   color(Color::Red);
      elements.push_back(std::move(block));
      elements.push_back(separator());
    } else if (!status_message_.empty()) {
      auto block = make_wrapped_block("ℹ️  " + status_message_, wrap_width) |
                   color(Color::Cyan);
      elements.push_back(std::move(block));
      elements.push_back(separator());
    }

    // Server selector section
    elements.push_back(text("📡 Server:") | bold);
    if (servers_.empty()) {
      elements.push_back(text("  No servers configured") | dim);
    } else {
      for (size_t i = 0; i < servers_.size(); ++i) {
        const auto& server = servers_[i];
        bool selected = (i == static_cast<size_t>(selected_server_index_));

        std::string line = "  ";
        if (selected) line += "▶ ";
        else line += "  ";

        line += server.label + " (" + server.host + ")";
        line += " - " + server.status;
        line += " [" + std::to_string(server.container_count) + " containers]";

        auto elem = text(line);
        if (selected) {
          elem = elem | bold | color(Color::Cyan);
        }
        elements.push_back(elem);
      }
    }

    elements.push_back(separator());

    // Tab content
    switch (current_tab_) {
      case Tab::CONTAINERS:
        render_containers_tab(elements);
        break;
      case Tab::IMAGES:
        render_images_tab(elements);
        break;
      case Tab::VOLUMES:
        render_volumes_tab(elements);
        break;
      case Tab::NETWORKS:
        render_networks_tab(elements);
        break;
      case Tab::STACKS:
        render_stacks_tab(elements);
        break;
    }

    elements.push_back(separator());

    // Help footer - context sensitive
    std::vector<Element> help;
    help.push_back(text("Tab: Next tab") | dim);
    help.push_back(text(" | ") | dim);
    help.push_back(text("Shift-Tab: Prev tab") | dim);
    help.push_back(text(" | ") | dim);
    help.push_back(text("1-5: Tabs") | dim);
    help.push_back(text(" | ") | dim);
    help.push_back(text("↑↓: Navigate") | dim);
    help.push_back(text(" | ") | dim);
    help.push_back(text("←→: Server") | dim);
    help.push_back(text(" | ") | dim);
    help.push_back(text("h: Hello-world") | dim);
    help.push_back(text(" | ") | dim);

    if (current_tab_ == Tab::CONTAINERS) {
      help.push_back(text("s: Start") | dim);
      help.push_back(text(" | ") | dim);
      help.push_back(text("S: Stop") | dim);
      help.push_back(text(" | ") | dim);
      help.push_back(text("r: Restart") | dim);
      help.push_back(text(" | ") | dim);
      help.push_back(text("l: Logs") | dim);
      help.push_back(text(" | ") | dim);
    } else if (current_tab_ == Tab::STACKS) {
      help.push_back(text("s: Start stack") | dim);
      help.push_back(text(" | ") | dim);
      help.push_back(text("S: Stop stack") | dim);
      help.push_back(text(" | ") | dim);
      help.push_back(text("r: Restart stack") | dim);
      help.push_back(text(" | ") | dim);
    }

    help.push_back(text("R: Refresh") | dim);
    help.push_back(text(" | ") | dim);
    help.push_back(text(":back: Exit") | dim);

    elements.push_back(hbox(help));

    return vbox(elements);
  };

  // Create event handler
  auto event_fn = [this](const Event& event) -> bool {
    // Server navigation with arrow keys
    if (event == Event::ArrowLeft) {
      if (selected_server_index_ > 0) {
        on_server_selected(selected_server_index_ - 1);
        return true;
      }
      return false;
    }
    if (event == Event::ArrowRight) {
      if (selected_server_index_ < static_cast<int>(servers_.size()) - 1) {
        on_server_selected(selected_server_index_ + 1);
        return true;
      }
      return false;
    }

    // Item navigation within active tab
    if (event == Event::ArrowUp) {
      switch (current_tab_) {
        case Tab::CONTAINERS:
          if (selected_container_index_ > 0) {
            --selected_container_index_;
            on_container_selected(selected_container_index_);
          }
          break;
        case Tab::IMAGES:
          if (selected_image_index_ > 0) {
            --selected_image_index_;
          }
          break;
        case Tab::VOLUMES:
          if (selected_volume_index_ > 0) {
            --selected_volume_index_;
          }
          break;
        case Tab::NETWORKS:
          if (selected_network_index_ > 0) {
            --selected_network_index_;
          }
          break;
        case Tab::STACKS:
          if (selected_stack_index_ > 0) {
            --selected_stack_index_;
            on_stack_selected(selected_stack_index_);
          }
          break;
      }
      return true;
    }

    if (event == Event::ArrowDown) {
      switch (current_tab_) {
        case Tab::CONTAINERS:
          if (selected_container_index_ < static_cast<int>(containers_.size()) - 1) {
            ++selected_container_index_;
            on_container_selected(selected_container_index_);
          }
          break;
        case Tab::IMAGES:
          if (selected_image_index_ < static_cast<int>(images_.size()) - 1) {
            ++selected_image_index_;
          }
          break;
        case Tab::VOLUMES:
          if (selected_volume_index_ < static_cast<int>(volumes_.size()) - 1) {
            ++selected_volume_index_;
          }
          break;
        case Tab::NETWORKS:
          if (selected_network_index_ < static_cast<int>(networks_.size()) - 1) {
            ++selected_network_index_;
          }
          break;
        case Tab::STACKS:
          if (selected_stack_index_ < static_cast<int>(stacks_.size()) - 1) {
            ++selected_stack_index_;
            on_stack_selected(selected_stack_index_);
          }
          break;
      }
      return true;
    }

    return false;
  };

  // Create interactive FTXUI component wrapper and set as root
  auto component = std::make_unique<FTXUIComponent>("docker-dashboard", render_fn, event_fn);
  set_root(std::move(component));

  if (log_) {
    log_->info("DockerMenu", "Docker dashboard menu built successfully");
  }
}

void DockerMenu::render_containers_tab(std::vector<Element>& elements) {
  if (loading_containers_) {
    elements.push_back(text("⏳ Loading containers...") | dim);
    return;
  }

  if (containers_.empty() && current_server_id_ != 0) {
    elements.push_back(text("No containers found") | dim);
    return;
  }

  if (!containers_.empty()) {
    elements.push_back(text("🐳 Containers:") | bold);

    // Table header
    elements.push_back(
      hbox({
        text("  NAME") | size(WIDTH, EQUAL, 30),
        text("STATE") | size(WIDTH, EQUAL, 12),
        text("IMAGE") | size(WIDTH, EQUAL, 35),
        text("STATUS")
      }) | bold
    );

    // Container rows
    for (size_t i = 0; i < containers_.size(); ++i) {
      const auto& container = containers_[i];
      bool selected = (i == static_cast<size_t>(selected_container_index_));

      // Apply filter
      if (!container_filter_.empty()) {
        if (container.name.find(container_filter_) == std::string::npos &&
            container.image.find(container_filter_) == std::string::npos) {
          continue;
        }
      }

      // Hide stopped containers if filter enabled
      if (!show_stopped_containers_ && container.state != "running") {
        continue;
      }

      std::string prefix = selected ? "▶ " : "  ";

      // State color and icon
      Color state_color = Color::Default;
      std::string state_icon = "●";
      if (container.state == "running") {
        state_color = Color::Green;
        state_icon = "●";
      } else if (container.state == "exited") {
        state_color = Color::Red;
        state_icon = "■";
      } else if (container.state == "paused") {
        state_color = Color::Yellow;
        state_icon = "⏸";
      }

      auto row = hbox({
        text(prefix + container.name) | size(WIDTH, EQUAL, 30),
        hbox({
          text(state_icon) | color(state_color),
          text(" " + container.state)
        }) | size(WIDTH, EQUAL, 12),
        text(container.image) | size(WIDTH, EQUAL, 35) | dim,
        text(container.status)
      });

      if (selected) {
        row = row | bgcolor(Color::Blue);
      }

      elements.push_back(row);
    }
  }
}

void DockerMenu::render_images_tab(std::vector<Element>& elements) {
  if (loading_images_) {
    elements.push_back(text("⏳ Loading images...") | dim);
    return;
  }

  elements.push_back(text("💿 Images:") | bold);

  if (images_.empty()) {
    elements.push_back(text("  No images found") | dim);
    return;
  }

  // Table header
  elements.push_back(
    hbox({
      text("  REPOSITORY") | size(WIDTH, EQUAL, 40),
      text("TAG") | size(WIDTH, EQUAL, 20),
      text("SIZE") | size(WIDTH, EQUAL, 15),
      text("CREATED")
    }) | bold
  );

  // Image rows
  for (size_t i = 0; i < images_.size(); ++i) {
    const auto& image = images_[i];
    bool selected = (i == static_cast<size_t>(selected_image_index_));

    std::string prefix = selected ? "▶ " : "  ";

    auto row = hbox({
      text(prefix + image.repository) | size(WIDTH, EQUAL, 40),
      text(image.tag) | size(WIDTH, EQUAL, 20) | color(Color::Cyan),
      text(image.size) | size(WIDTH, EQUAL, 15),
      text(image.created) | dim
    });

    if (selected) {
      row = row | bgcolor(Color::Blue);
    }

    elements.push_back(row);
  }
}

void DockerMenu::render_volumes_tab(std::vector<Element>& elements) {
  if (loading_volumes_) {
    elements.push_back(text("⏳ Loading volumes...") | dim);
    return;
  }

  elements.push_back(text("💾 Volumes:") | bold);

  if (volumes_.empty()) {
    elements.push_back(text("  No volumes found") | dim);
    return;
  }

  // Table header
  elements.push_back(
    hbox({
      text("  NAME") | size(WIDTH, EQUAL, 40),
      text("DRIVER") | size(WIDTH, EQUAL, 15),
      text("MOUNTPOINT")
    }) | bold
  );

  // Volume rows
  for (size_t i = 0; i < volumes_.size(); ++i) {
    const auto& volume = volumes_[i];
    bool selected = (i == static_cast<size_t>(selected_volume_index_));

    std::string prefix = selected ? "▶ " : "  ";

    auto row = hbox({
      text(prefix + volume.name) | size(WIDTH, EQUAL, 40),
      text(volume.driver) | size(WIDTH, EQUAL, 15) | color(Color::Cyan),
      text(volume.mountpoint) | dim
    });

    if (selected) {
      row = row | bgcolor(Color::Blue);
    }

    elements.push_back(row);
  }
}

void DockerMenu::render_networks_tab(std::vector<Element>& elements) {
  if (loading_networks_) {
    elements.push_back(text("⏳ Loading networks...") | dim);
    return;
  }

  elements.push_back(text("🌐 Networks:") | bold);

  if (networks_.empty()) {
    elements.push_back(text("  No networks found") | dim);
    return;
  }

  // Table header
  elements.push_back(
    hbox({
      text("  NAME") | size(WIDTH, EQUAL, 30),
      text("ID") | size(WIDTH, EQUAL, 15),
      text("DRIVER") | size(WIDTH, EQUAL, 15),
      text("SCOPE")
    }) | bold
  );

  // Network rows
  for (size_t i = 0; i < networks_.size(); ++i) {
    const auto& network = networks_[i];
    bool selected = (i == static_cast<size_t>(selected_network_index_));

    std::string prefix = selected ? "▶ " : "  ";
    std::string short_id = network.id.substr(0, 12);

    auto row = hbox({
      text(prefix + network.name) | size(WIDTH, EQUAL, 30),
      text(short_id) | size(WIDTH, EQUAL, 15) | dim,
      text(network.driver) | size(WIDTH, EQUAL, 15) | color(Color::Cyan),
      text(network.scope)
    });

    if (selected) {
      row = row | bgcolor(Color::Blue);
    }

    elements.push_back(row);
  }
}

void DockerMenu::render_stacks_tab(std::vector<Element>& elements) {
  if (loading_stacks_) {
    elements.push_back(text("⏳ Loading stacks...") | dim);
    return;
  }

  elements.push_back(text("📦 Stacks:") | bold);

  if (stacks_.empty()) {
    elements.push_back(text("  No stacks found") | dim);
    elements.push_back(text("  Use 'nazg docker stack create' to create stacks") | dim);
    return;
  }

  // Stack list
  for (size_t i = 0; i < stacks_.size(); ++i) {
    const auto& stack = stacks_[i];
    bool selected = (i == static_cast<size_t>(selected_stack_index_));

    std::string line = selected ? "▶ " : "  ";
    line += "📦 " + stack.name;
    line += " (" + std::to_string(stack.service_count) + " services)";
    if (!stack.description.empty()) {
      line += " - " + stack.description;
    }

    auto elem = text(line);
    if (selected) {
      elem = elem | bold | bgcolor(Color::Blue);
    }
    elements.push_back(elem);
  }
}

void DockerMenu::on_load() {
  if (log_) {
    log_->info("DockerMenu", "Loading Docker dashboard");
  }

  register_commands();
  register_key_bindings();

  load_servers();

  if (!servers_.empty()) {
    if (selected_server_index_ < 0 ||
        selected_server_index_ >= static_cast<int>(servers_.size())) {
      selected_server_index_ = 0;
    }
    on_server_selected(selected_server_index_);
  } else if (error_message_.empty()) {
    set_error("No Docker servers configured");
  }
}

void DockerMenu::on_unload() {
  unregister_key_bindings();
  if (log_) {
    log_->info("DockerMenu", "Unloading Docker dashboard");
  }
}

void DockerMenu::on_resume() {
  if (log_) {
    log_->info("DockerMenu", "Resuming Docker dashboard");
  }

  register_key_bindings();
  on_refresh();

  if (error_message_.empty()) {
    set_status("Refreshed docker data");
  }
}

Menu::MenuState DockerMenu::save_state() const {
  MenuState state;
  state.data["selected_server_index"] = std::to_string(selected_server_index_);
  state.data["selected_container_index"] = std::to_string(selected_container_index_);
  state.data["selected_stack_index"] = std::to_string(selected_stack_index_);
  state.data["selected_image_index"] = std::to_string(selected_image_index_);
  state.data["selected_volume_index"] = std::to_string(selected_volume_index_);
  state.data["selected_network_index"] = std::to_string(selected_network_index_);
  state.data["current_server_id"] = std::to_string(current_server_id_);
  state.data["current_tab"] = std::to_string(static_cast<int>(current_tab_));
  state.data["container_filter"] = container_filter_;
  state.data["show_stopped"] = show_stopped_containers_ ? "1" : "0";
  return state;
}

void DockerMenu::restore_state(const MenuState& state) {
  auto it = state.data.find("selected_server_index");
  if (it != state.data.end()) {
    selected_server_index_ = std::stoi(it->second);
  }

  it = state.data.find("selected_container_index");
  if (it != state.data.end()) {
    selected_container_index_ = std::stoi(it->second);
  }

  it = state.data.find("selected_stack_index");
  if (it != state.data.end()) {
    selected_stack_index_ = std::stoi(it->second);
  }

  it = state.data.find("selected_image_index");
  if (it != state.data.end()) {
    selected_image_index_ = std::stoi(it->second);
  }

  it = state.data.find("selected_volume_index");
  if (it != state.data.end()) {
    selected_volume_index_ = std::stoi(it->second);
  }

  it = state.data.find("selected_network_index");
  if (it != state.data.end()) {
    selected_network_index_ = std::stoi(it->second);
  }

  it = state.data.find("current_server_id");
  if (it != state.data.end()) {
    current_server_id_ = std::stoll(it->second);
  }

  it = state.data.find("current_tab");
  if (it != state.data.end()) {
    current_tab_ = static_cast<Tab>(std::stoi(it->second));
  }

  it = state.data.find("container_filter");
  if (it != state.data.end()) {
    container_filter_ = it->second;
  }

  it = state.data.find("show_stopped");
  if (it != state.data.end()) {
    show_stopped_containers_ = (it->second == "1");
  }

  if (log_) {
    log_->info("DockerMenu", "State restored");
  }
}

// Data loading methods

void DockerMenu::load_servers() {
  servers_.clear();
  error_message_.clear();

  try {
    auto server_rows = store_->list_servers();

    for (const auto& row : server_rows) {
      ServerInfo server;
      server.id = std::stoll(row.at("id"));
      server.label = row.at("label");
      server.host = row.at("host");

      auto status_it = row.find("agent_status");
      server.status = (status_it != row.end()) ? status_it->second : "unknown";

      // Count containers for this server
      auto containers = store_->list_containers(server.id);
      server.container_count = static_cast<int>(containers.size());

      servers_.push_back(server);
    }

    if (log_) {
      log_->info("DockerMenu", "Loaded " + std::to_string(servers_.size()) + " servers");
    }
  } catch (const std::exception& e) {
    std::string message = std::string("Failed to load servers: ") + e.what();
    set_error(message);
    if (log_) {
      log_->error("DockerMenu", message);
    }
  }
}

void DockerMenu::load_containers(int64_t server_id) {
  containers_.clear();
  loading_containers_ = true;
  error_message_.clear();

  try {
    auto container_rows = store_->list_containers(server_id);

    for (const auto& row : container_rows) {
      ContainerInfo container;
      container.id = row.at("container_id");
      container.name = row.at("name");
      container.image = row.at("image");
      container.state = row.at("state");
      container.status = row.at("status");

      auto health_it = row.find("health_status");
      container.health_status = (health_it != row.end()) ? health_it->second : "";

      auto service_it = row.find("service_name");
      container.service_name = (service_it != row.end()) ? service_it->second : "";

      containers_.push_back(container);
    }

    if (log_) {
      log_->info("DockerMenu",
                 "Loaded " + std::to_string(containers_.size()) +
                 " containers for server " + std::to_string(server_id));
    }
  } catch (const std::exception& e) {
    std::string message = std::string("Failed to load containers: ") + e.what();
    set_error(message);
    if (log_) {
      log_->error("DockerMenu", message);
    }
  }

  loading_containers_ = false;
}

void DockerMenu::load_stacks(int64_t server_id) {
  stacks_.clear();
  loading_stacks_ = true;

  if (!orchestrator_) {
    loading_stacks_ = false;
    return;
  }

  try {
    // Load stacks from orchestrator
    auto stack_profiles = orchestrator_->list_stacks(server_id);

    for (const auto& profile : stack_profiles) {
      StackInfo stack;
      stack.id = profile.id;
      stack.name = profile.name;
      stack.description = profile.description;
      stack.service_count = static_cast<int>(profile.compose_files.size());
      stack.priority = profile.priority;
      stacks_.push_back(stack);
    }

    if (log_) {
      log_->info("DockerMenu", "Loaded " + std::to_string(stacks_.size()) + " stacks");
    }
  } catch (const std::exception& e) {
    std::string message = std::string("Failed to load stacks: ") + e.what();
    set_error(message);
    if (log_) {
      log_->error("DockerMenu", message);
    }
  }

  loading_stacks_ = false;
}

void DockerMenu::load_images(int64_t server_id) {
  images_.clear();
  loading_images_ = true;

  // TODO: Query database for images
  // For now, populate with placeholder
  (void)server_id;

  loading_images_ = false;
}

void DockerMenu::load_volumes(int64_t server_id) {
  volumes_.clear();
  loading_volumes_ = true;

  // TODO: Query database for volumes
  // For now, populate with placeholder
  (void)server_id;

  loading_volumes_ = false;
}

void DockerMenu::load_networks(int64_t server_id) {
  networks_.clear();
  loading_networks_ = true;

  // TODO: Query database for networks
  // For now, populate with placeholder
  (void)server_id;

  loading_networks_ = false;
}

void DockerMenu::switch_tab(Tab tab) {
  if (current_tab_ == tab) {
    return;
  }

  current_tab_ = tab;
  error_message_.clear();

  if (log_) {
    log_->info("DockerMenu", "Switched to tab: " + std::to_string(static_cast<int>(tab)));
  }
}

std::string DockerMenu::tab_label(Tab tab) const {
  switch (tab) {
    case Tab::CONTAINERS:
      return "Containers";
    case Tab::IMAGES:
      return "Images";
    case Tab::VOLUMES:
      return "Volumes";
    case Tab::NETWORKS:
      return "Networks";
    case Tab::STACKS:
      return "Stacks";
  }
  return "Unknown";
}

void DockerMenu::cycle_tab(int direction) {
  static constexpr std::array<Tab, 5> order{
      Tab::CONTAINERS, Tab::IMAGES, Tab::VOLUMES, Tab::NETWORKS, Tab::STACKS};

  auto it = std::find(order.begin(), order.end(), current_tab_);
  if (it == order.end()) {
    switch_tab(order.front());
  } else {
    const int size = static_cast<int>(order.size());
    int index = static_cast<int>(std::distance(order.begin(), it));
    index = (index + direction) % size;
    if (index < 0) {
      index += size;
    }
    switch_tab(order[static_cast<size_t>(index)]);
  }

  set_status("Tab: " + tab_label(current_tab_));
}

void DockerMenu::register_commands() {
  if (!ctx_ || commands_registered_) {
    return;
  }

  auto& mgr = ctx_->commands();

  auto make_handler = [](auto&& fn) {
    return [fn](TUIContext& ctx, const std::vector<std::string>& args) -> bool {
      (void)args;
      auto* menu = dynamic_cast<DockerMenu*>(ctx.active_menu());
      if (!menu) {
        ctx.set_status_message("Docker menu not active");
        return false;
      }
      return fn(*menu);
    };
  };

  mgr.register_command("docker-tab-next", "Docker dashboard: next tab",
      make_handler([](DockerMenu& menu) {
        menu.cycle_tab(+1);
        return true;
      }));

  mgr.register_command("docker-tab-prev", "Docker dashboard: previous tab",
      make_handler([](DockerMenu& menu) {
        menu.cycle_tab(-1);
        return true;
      }));

  mgr.register_command("docker-tab-containers", "Docker dashboard: containers tab",
      make_handler([](DockerMenu& menu) {
        menu.switch_tab(Tab::CONTAINERS);
        menu.set_status("Tab: " + menu.tab_label(Tab::CONTAINERS));
        return true;
      }));

  mgr.register_command("docker-tab-images", "Docker dashboard: images tab",
      make_handler([](DockerMenu& menu) {
        menu.switch_tab(Tab::IMAGES);
        menu.set_status("Tab: " + menu.tab_label(Tab::IMAGES));
        return true;
      }));

  mgr.register_command("docker-tab-volumes", "Docker dashboard: volumes tab",
      make_handler([](DockerMenu& menu) {
        menu.switch_tab(Tab::VOLUMES);
        menu.set_status("Tab: " + menu.tab_label(Tab::VOLUMES));
        return true;
      }));

  mgr.register_command("docker-tab-networks", "Docker dashboard: networks tab",
      make_handler([](DockerMenu& menu) {
        menu.switch_tab(Tab::NETWORKS);
        menu.set_status("Tab: " + menu.tab_label(Tab::NETWORKS));
        return true;
      }));

  mgr.register_command("docker-tab-stacks", "Docker dashboard: stacks tab",
      make_handler([](DockerMenu& menu) {
        menu.switch_tab(Tab::STACKS);
        menu.set_status("Tab: " + menu.tab_label(Tab::STACKS));
        return true;
      }));

  mgr.register_command("docker-refresh", "Docker dashboard: refresh data",
      make_handler([](DockerMenu& menu) {
        menu.on_refresh();
        menu.set_status("Refreshed docker data");
        return true;
      }));

  mgr.register_command("docker-container-start", "Docker dashboard: start container",
      make_handler([](DockerMenu& menu) {
        menu.on_start_container();
        return true;
      }));

  mgr.register_command("docker-container-stop", "Docker dashboard: stop container",
      make_handler([](DockerMenu& menu) {
        menu.on_stop_container();
        return true;
      }));

  mgr.register_command("docker-container-restart", "Docker dashboard: restart container",
      make_handler([](DockerMenu& menu) {
        menu.on_restart_container();
        return true;
      }));

  mgr.register_command("docker-container-logs", "Docker dashboard: view logs",
      make_handler([](DockerMenu& menu) {
        menu.on_view_logs();
        return true;
      }));

  mgr.register_command("docker-run-hello-world", "Docker dashboard: run hello-world container",
      make_handler([](DockerMenu& menu) {
        return menu.on_run_hello_world();
      }));

  commands_registered_ = true;
}

void DockerMenu::register_key_bindings() {
  if (!ctx_) {
    return;
  }

  if (key_bindings_active_) {
    unregister_key_bindings();
  }

  active_key_bindings_.clear();

  auto add_binding = [&](const std::string& key, const std::string& command, const std::string& desc) {
    KeyBinding binding{key, command, desc, Mode::NORMAL, false};
    ctx_->keys().bind(binding);
    active_key_bindings_.push_back(binding);
  };

  add_binding("Tab", "docker-tab-next", "Docker: next tab");
  add_binding("Shift-Tab", "docker-tab-prev", "Docker: previous tab");
  add_binding("1", "docker-tab-containers", "Docker: containers tab");
  add_binding("2", "docker-tab-images", "Docker: images tab");
  add_binding("3", "docker-tab-volumes", "Docker: volumes tab");
  add_binding("4", "docker-tab-networks", "Docker: networks tab");
  add_binding("5", "docker-tab-stacks", "Docker: stacks tab");
  add_binding("R", "docker-refresh", "Docker: refresh data");
  add_binding("s", "docker-container-start", "Docker: start container");
  add_binding("S", "docker-container-stop", "Docker: stop container");
  add_binding("r", "docker-container-restart", "Docker: restart container");
  add_binding("l", "docker-container-logs", "Docker: view logs");
  add_binding("h", "docker-run-hello-world", "Docker: run hello-world");

  key_bindings_active_ = true;
}

void DockerMenu::unregister_key_bindings() {
  if (!ctx_ || active_key_bindings_.empty()) {
    active_key_bindings_.clear();
    key_bindings_active_ = false;
    return;
  }

  for (const auto& binding : active_key_bindings_) {
    ctx_->keys().unbind(binding.key, binding.mode, binding.is_prefix);
  }

  active_key_bindings_.clear();
  key_bindings_active_ = false;
}

void DockerMenu::set_status(const std::string& message) {
  status_message_ = message;
  if (!message.empty()) {
    error_message_.clear();
  }
  if (ctx_) {
    ctx_->set_status_message(message);
  }
}

void DockerMenu::set_error(const std::string& message) {
  error_message_ = message;
  status_message_.clear();
  if (ctx_) {
    ctx_->set_status_message(message);
  }
}

std::string DockerMenu::first_line(std::string_view text) const {
  if (text.empty()) {
    return "";
  }
  auto pos = text.find_first_of("\r\n");
  std::string line{text.substr(0, pos)};
  constexpr size_t kMax = 120;
  if (line.size() > kMax) {
    line = line.substr(0, kMax - 3) + "...";
  }
  return line;
}

bool DockerMenu::on_run_hello_world() {
  if (!store_) {
    set_error("Docker store unavailable");
    return false;
  }

  if (servers_.empty() ||
      selected_server_index_ < 0 ||
      selected_server_index_ >= static_cast<int>(servers_.size())) {
    set_error("No server selected");
    return false;
  }

  const auto& server = servers_[selected_server_index_];
  auto result = docker_monitor::run_hello_world_container(server.id, store_, log_);

  if (result.success) {
    std::string line = first_line(result.output);
    if (line.empty()) {
      line = "hello-world completed";
    }
    set_status("hello-world on " + server.label + ": " + line);
    return true;
  }

  std::string line = first_line(result.error);
  if (line.empty()) {
    line = "hello-world failed with exit code " + std::to_string(result.exit_code);
  }
  set_error("hello-world on " + server.label + " failed: " + line);
  return false;
}

// Event handlers

void DockerMenu::on_server_selected(int index) {
  if (index < 0 || index >= static_cast<int>(servers_.size())) {
    return;
  }

  selected_server_index_ = index;
  current_server_id_ = servers_[index].id;
  load_containers(current_server_id_);
  load_stacks(current_server_id_);
  load_images(current_server_id_);
  load_volumes(current_server_id_);
  load_networks(current_server_id_);

  selected_container_index_ = 0;
  selected_stack_index_ = 0;
  selected_image_index_ = 0;
  selected_volume_index_ = 0;
  selected_network_index_ = 0;

  if (error_message_.empty()) {
    set_status("Server: " + servers_[index].label);
  }

  if (log_) {
    log_->info("DockerMenu", "Selected server: " + servers_[index].label);
  }
}

void DockerMenu::on_container_selected(int index) {
  if (index >= 0 && index < static_cast<int>(containers_.size())) {
    selected_container_index_ = index;

    if (log_) {
      log_->info("DockerMenu", "Selected container: " + containers_[index].name);
    }
  }
}

void DockerMenu::on_stack_selected(int index) {
  if (index >= 0 && index < static_cast<int>(stacks_.size())) {
    selected_stack_index_ = index;

    if (log_) {
      log_->info("DockerMenu", "Selected stack: " + stacks_[index].name);
    }
  }
}

void DockerMenu::on_start_container() {
  if (selected_container_index_ >= static_cast<int>(containers_.size())) {
    return;
  }

  const auto& container = containers_[selected_container_index_];

  if (log_) {
    log_->info("DockerMenu", "Starting container: " + container.name);
  }

  set_status("Starting container: " + container.name);
  // TODO: Implement actual container start via agent
}

void DockerMenu::on_stop_container() {
  if (selected_container_index_ >= static_cast<int>(containers_.size())) {
    return;
  }

  const auto& container = containers_[selected_container_index_];

  if (log_) {
    log_->info("DockerMenu", "Stopping container: " + container.name);
  }

  set_status("Stopping container: " + container.name);
  // TODO: Implement actual container stop via agent
}

void DockerMenu::on_restart_container() {
  if (selected_container_index_ >= static_cast<int>(containers_.size())) {
    return;
  }

  const auto& container = containers_[selected_container_index_];

  if (log_) {
    log_->info("DockerMenu", "Restarting container: " + container.name);
  }

  set_status("Restarting container: " + container.name);
  // TODO: Implement actual container restart via agent
}

void DockerMenu::on_view_logs() {
  if (selected_container_index_ >= static_cast<int>(containers_.size())) {
    return;
  }

  const auto& container = containers_[selected_container_index_];

  if (log_) {
    log_->info("DockerMenu", "Viewing logs for container: " + container.name);
  }

  set_status("Log viewer coming soon for: " + container.name);
  // TODO: Open logs view (could be another menu or modal)
}

void DockerMenu::on_refresh() {
  if (log_) {
    log_->info("DockerMenu", "Refreshing data");
  }

  load_servers();

  if (!servers_.empty()) {
    if (selected_server_index_ < 0 ||
        selected_server_index_ >= static_cast<int>(servers_.size())) {
      selected_server_index_ = 0;
    }
    on_server_selected(selected_server_index_);
  } else {
    current_server_id_ = 0;
    if (error_message_.empty()) {
      set_error("No Docker servers configured");
    }
  }
}

// UI helpers

std::string DockerMenu::format_container_status(const ContainerInfo& container) const {
  std::string result = container.state;

  if (!container.health_status.empty() && container.health_status != "none") {
    result += " (" + container.health_status + ")";
  }

  return result;
}

std::string DockerMenu::get_status_color(const std::string& state) const {
  if (state == "running") return "green";
  if (state == "exited") return "red";
  if (state == "paused") return "yellow";
  if (state == "restarting") return "blue";
  return "gray";
}

} // namespace nazg::tui

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

#include "bot/registry.hpp"
#include "bot/doctor.hpp"
#include "bot/git_doctor.hpp"
#include <stdexcept>

namespace nazg::bot {

void registry::register_bot(const BotSpec& spec, BotFactory factory) {
  specs_[spec.name] = spec;
  factories_[spec.name] = factory;
}

std::optional<BotSpec> registry::get_spec(const std::string& name) const {
  auto it = specs_.find(name);
  if (it != specs_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::vector<BotSpec> registry::list_bots() const {
  std::vector<BotSpec> result;
  result.reserve(specs_.size());
  for (const auto& [name, spec] : specs_) {
    result.push_back(spec);
  }
  return result;
}

std::unique_ptr<BotBase> registry::create_bot(
    const std::string& name,
    const HostConfig& host,
    ::nazg::config::store* cfg,
    ::nazg::nexus::Store* store,
    ::nazg::blackbox::logger* log) const {

  auto it = factories_.find(name);
  if (it == factories_.end()) {
    throw std::runtime_error("Unknown bot: " + name);
  }

  return it->second(host, cfg, store, log);
}

registry& get_registry() {
  static registry reg;
  return reg;
}

void register_builtin_bots(registry& reg) {
  // Register Doctor Bot
  BotSpec doctor_spec;
  doctor_spec.name = "doctor";
  doctor_spec.description = "System health diagnostics (CPU, memory, disk, services, network)";
  doctor_spec.required_inputs = {};  // No required inputs, uses host config

  reg.register_bot(doctor_spec, [](const HostConfig& host,
                                    ::nazg::config::store* cfg,
                                    ::nazg::nexus::Store* store,
                                    ::nazg::blackbox::logger* log) -> std::unique_ptr<BotBase> {
    return std::make_unique<DoctorBot>(host, cfg, store, log);
  });

  // Register Git Doctor Bot
  BotSpec git_doctor_spec;
  git_doctor_spec.name = "git-doctor";
  git_doctor_spec.description = "Git server infrastructure health (cgit/gitea, nginx, fcgiwrap, repos)";
  git_doctor_spec.required_inputs = {};  // No required inputs, uses host config

  reg.register_bot(git_doctor_spec, [](const HostConfig& host,
                                        ::nazg::config::store* cfg,
                                        ::nazg::nexus::Store* store,
                                        ::nazg::blackbox::logger* log) -> std::unique_ptr<BotBase> {
    return std::make_unique<GitDoctorBot>(host, cfg, store, log);
  });
}

} // namespace nazg::bot

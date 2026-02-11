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

#include "directive/context.hpp"

#include <filesystem>
#include <string>

namespace nazg::directive {

// Generate a random alpha-numeric token (used for temp file/service names)
std::string random_token(std::size_t length = 10);

// Build the nazg-agent container image and export it to a tarball.
// Returns true on success and fills out_tag with the temporary image tag,
// out_tar with the tarball path, and out_temp_dir with the temp directory
// that should be cleaned up by the caller.
bool build_agent_container_image(const directive::context &ectx,
                                 const std::string &base_image,
                                 std::string &out_tag,
                                 std::filesystem::path &out_tar,
                                 std::filesystem::path &out_temp_dir);

// Default location where nazg should persist the pre-built agent container.
// This resolves XDG_STATE_HOME (or falls back to ~/.local/state).
std::filesystem::path default_agent_tarball_path();

} // namespace nazg::directive


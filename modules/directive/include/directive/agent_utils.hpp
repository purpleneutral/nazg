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


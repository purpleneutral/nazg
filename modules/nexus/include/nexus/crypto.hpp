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
#include <optional>
#include <string>

namespace nazg::nexus {

/// Encrypt a plaintext token for database storage.
/// Returns "enc:v1:<hex-iv>:<hex-ciphertext>:<hex-tag>" or empty on error.
/// The salt must be a hex-encoded string (from nazg_metadata table).
std::string encrypt_token(const std::string &plaintext,
                          const std::string &salt_hex);

/// Decrypt a stored token. If the value does not have the "enc:v1:" prefix,
/// returns it unchanged (backwards compatibility with plaintext values).
/// Returns std::nullopt on decryption failure.
std::optional<std::string> decrypt_token(const std::string &stored,
                                         const std::string &salt_hex);

} // namespace nazg::nexus

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

#include "nexus/crypto.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace nazg::nexus {

namespace {

constexpr int kKeyLen = 32;    // AES-256
constexpr int kIvLen = 12;     // GCM standard
constexpr int kTagLen = 16;    // Full GCM tag
constexpr int kPbkdf2Iters = 100000;
constexpr const char *kPrefix = "enc:v1:";
constexpr size_t kPrefixLen = 7;

std::string to_hex(const unsigned char *data, size_t len) {
  std::ostringstream oss;
  for (size_t i = 0; i < len; ++i) {
    oss << std::hex << std::setfill('0') << std::setw(2)
        << static_cast<int>(data[i]);
  }
  return oss.str();
}

std::vector<unsigned char> from_hex(const std::string &hex) {
  std::vector<unsigned char> bytes;
  if (hex.size() % 2 != 0) return bytes;
  bytes.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    unsigned int byte = 0;
    std::istringstream iss(hex.substr(i, 2));
    iss >> std::hex >> byte;
    if (iss.fail()) {
      bytes.clear();
      return bytes;
    }
    bytes.push_back(static_cast<unsigned char>(byte));
  }
  return bytes;
}

std::string get_machine_secret() {
  // Prefer explicit env var override (useful for containers / non-Linux)
  const char *env_key = std::getenv("NAZG_TOKEN_KEY");
  if (env_key && env_key[0] != '\0') {
    return std::string(env_key);
  }

  // Fall back to /etc/machine-id (Linux/systemd)
  std::ifstream f("/etc/machine-id");
  if (f) {
    std::string id;
    std::getline(f, id);
    // Trim trailing whitespace
    while (!id.empty() && (id.back() == '\n' || id.back() == '\r' ||
                           id.back() == ' ')) {
      id.pop_back();
    }
    if (!id.empty()) return id;
  }

  // Last resort: use a fixed fallback (still better than plaintext since the
  // salt adds per-database uniqueness)
  return "nazg-default-key-fallback";
}

bool derive_key(const std::string &secret, const std::vector<unsigned char> &salt,
                unsigned char *out_key) {
  return PKCS5_PBKDF2_HMAC(secret.c_str(),
                            static_cast<int>(secret.size()),
                            salt.data(),
                            static_cast<int>(salt.size()),
                            kPbkdf2Iters,
                            EVP_sha256(),
                            kKeyLen,
                            out_key) == 1;
}

} // namespace

std::string encrypt_token(const std::string &plaintext,
                          const std::string &salt_hex) {
  if (plaintext.empty()) return "";

  auto salt = from_hex(salt_hex);
  if (salt.empty()) return "";

  std::string secret = get_machine_secret();
  unsigned char key[kKeyLen];
  if (!derive_key(secret, salt, key)) return "";

  // Generate random IV
  unsigned char iv[kIvLen];
  if (RAND_bytes(iv, kIvLen) != 1) return "";

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return "";

  std::string result;
  int len = 0;
  int ciphertext_len = 0;
  std::vector<unsigned char> ciphertext(plaintext.size() + kTagLen);

  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, iv) != 1 ||
      EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                        reinterpret_cast<const unsigned char *>(plaintext.c_str()),
                        static_cast<int>(plaintext.size())) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return "";
  }
  ciphertext_len = len;

  if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return "";
  }
  ciphertext_len += len;

  unsigned char tag[kTagLen];
  if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return "";
  }

  EVP_CIPHER_CTX_free(ctx);

  // Format: enc:v1:<iv>:<ciphertext>:<tag>
  result = kPrefix;
  result += to_hex(iv, kIvLen);
  result += ":";
  result += to_hex(ciphertext.data(), static_cast<size_t>(ciphertext_len));
  result += ":";
  result += to_hex(tag, kTagLen);

  // Clear key from stack
  explicit_bzero(key, kKeyLen);

  return result;
}

std::optional<std::string> decrypt_token(const std::string &stored,
                                         const std::string &salt_hex) {
  if (stored.empty()) return "";

  // Backwards compatibility: plaintext values lack the prefix
  if (stored.size() < kPrefixLen ||
      stored.compare(0, kPrefixLen, kPrefix) != 0) {
    return stored;
  }

  // Parse enc:v1:<iv>:<ct>:<tag>
  std::string payload = stored.substr(kPrefixLen);
  size_t sep1 = payload.find(':');
  if (sep1 == std::string::npos) return std::nullopt;
  size_t sep2 = payload.find(':', sep1 + 1);
  if (sep2 == std::string::npos) return std::nullopt;

  auto iv = from_hex(payload.substr(0, sep1));
  auto ct = from_hex(payload.substr(sep1 + 1, sep2 - sep1 - 1));
  auto tag = from_hex(payload.substr(sep2 + 1));

  if (iv.size() != kIvLen || tag.size() != kTagLen || ct.empty()) {
    return std::nullopt;
  }

  auto salt = from_hex(salt_hex);
  if (salt.empty()) return std::nullopt;

  std::string secret = get_machine_secret();
  unsigned char key[kKeyLen];
  if (!derive_key(secret, salt, key)) return std::nullopt;

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return std::nullopt;

  std::vector<unsigned char> plaintext(ct.size());
  int len = 0;
  int plaintext_len = 0;

  bool ok =
      EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, iv.data()) == 1 &&
      EVP_DecryptUpdate(ctx, plaintext.data(), &len, ct.data(),
                        static_cast<int>(ct.size())) == 1;

  if (ok) {
    plaintext_len = len;
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                        const_cast<unsigned char *>(tag.data()));
    ok = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) == 1;
    if (ok) plaintext_len += len;
  }

  EVP_CIPHER_CTX_free(ctx);
  explicit_bzero(key, kKeyLen);

  if (!ok) return std::nullopt;

  return std::string(reinterpret_cast<char *>(plaintext.data()),
                     static_cast<size_t>(plaintext_len));
}

} // namespace nazg::nexus

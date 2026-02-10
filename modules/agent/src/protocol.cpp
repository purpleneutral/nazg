#include "agent/protocol.hpp"

#include <cstring>
#include <vector>

namespace nazg::agent::protocol {

std::vector<std::uint8_t> encode(const Header &header, const std::string &payload) {
  std::vector<std::uint8_t> buffer;
  buffer.reserve(sizeof(Header) + payload.size());

  Header local = header;
  local.payload_size = static_cast<std::uint32_t>(payload.size());

  std::uint8_t header_bytes[sizeof(Header)];
  std::memcpy(header_bytes, &local, sizeof(Header));
  buffer.insert(buffer.end(), header_bytes, header_bytes + sizeof(Header));

  buffer.insert(buffer.end(), payload.begin(), payload.end());
  return buffer;
}

bool decode(const std::vector<std::uint8_t> &buffer, Header &out_header, std::string &out_payload) {
  if (buffer.size() < sizeof(Header)) {
    return false;
  }

  std::memcpy(&out_header, buffer.data(), sizeof(Header));
  auto payload_size = static_cast<std::size_t>(out_header.payload_size);
  if (buffer.size() < sizeof(Header) + payload_size) {
    return false;
  }

  out_payload.assign(reinterpret_cast<const char *>(buffer.data() + sizeof(Header)), payload_size);
  return true;
}

} // namespace nazg::agent::protocol

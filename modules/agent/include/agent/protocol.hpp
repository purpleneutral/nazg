#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nazg::agent::protocol {

enum class MessageType : std::uint8_t {
  // Original protocol
  Hello = 1,
  HelloAck = 2,
  RunCommand = 3,
  CommandAck = 4,
  Heartbeat = 5,
  Log = 6,
  Result = 7,

  // Docker monitoring protocol (phone-home mode)
  Register = 10,           // Agent registers with control center
  RegisterAck = 11,        // Control center acknowledges registration
  DockerFullScan = 20,     // Full docker environment scan
  DockerEvent = 21,        // Single container event (start, stop, etc.)
  DockerIncrementalUpdate = 22,  // Incremental updates to container states

  // Control commands (sent from control center to agent)
  DockerCommand = 30,      // Execute docker command (restart, logs, etc.)
  DockerCommandResult = 31, // Result of docker command execution

  Error = 255,
};

struct Header {
  MessageType type = MessageType::Error;
  std::uint32_t payload_size = 0;
};

// Message payload structures (transported as JSON in practice)

// Register: Agent → Control Center
// {
//   "server_label": "media-server",
//   "agent_version": "0.1.0",
//   "hostname": "tank",
//   "capabilities": ["docker", "compose", "systemd"],
//   "system_info": {
//     "os": "Ubuntu 22.04",
//     "docker_version": "24.0.5",
//     "arch": "x86_64"
//   }
// }

// DockerFullScan: Agent → Control Center
// {
//   "server_label": "media-server",
//   "timestamp": 1698765432,
//   "containers": [
//     {
//       "id": "abc123def456",
//       "name": "plex",
//       "image": "linuxserver/plex:latest",
//       "state": "running",
//       "status": "Up 3 days",
//       "created": 1698000000,
//       "ports": [{"host": 32400, "container": 32400, "proto": "tcp"}],
//       "volumes": [{"host": "/tank/media", "container": "/data", "mode": "ro"}],
//       "networks": ["media-net"],
//       "compose_file": "/opt/media/docker-compose.yml",
//       "service_name": "plex",
//       "health_status": "healthy",
//       "restart_policy": "unless-stopped",
//       "labels": {"com.example.app": "plex"}
//     }
//   ],
//   "compose_files": [
//     {
//       "path": "/opt/media/docker-compose.yml",
//       "project_name": "media",
//       "services": ["plex", "sonarr", "radarr"],
//       "hash": "sha256:abc123..."
//     }
//   ],
//   "images": [...],
//   "networks": [...],
//   "volumes": [...]
// }

// DockerEvent: Agent → Control Center
// {
//   "server_label": "media-server",
//   "timestamp": 1698765432,
//   "container_id": "abc123def456",
//   "container_name": "plex",
//   "event": "restart",
//   "old_state": "running",
//   "new_state": "running",
//   "metadata": {
//     "exit_code": 0,
//     "reason": "manual"
//   }
// }

// DockerCommand: Control Center → Agent
// {
//   "command_id": 123,
//   "container": "plex",  // Container name or ID
//   "action": "restart",   // restart, stop, start, logs, inspect
//   "params": {
//     "timeout": 30
//   }
// }

// DockerCommandResult: Agent → Control Center
// {
//   "command_id": 123,
//   "success": true,
//   "exit_code": 0,
//   "output": "Container restarted successfully",
//   "error": ""
// }

std::vector<std::uint8_t> encode(const Header &header, const std::string &payload);
bool decode(const std::vector<std::uint8_t> &buffer, Header &out_header, std::string &out_payload);

} // namespace nazg::agent::protocol

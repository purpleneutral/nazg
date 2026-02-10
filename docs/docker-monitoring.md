# Docker Monitoring System Design

## Overview

Phase 1: Remote control center agents phone home with docker environment status.
Nazg stores this data in nexus, making the docker environment observable and queryable.
Control operations (restart, etc.) come in phase 2.

## Architecture

```
┌─────────────────────────────────────┐
│  Remote Server (media-server)       │
│  ┌────────────────────────────┐     │
│  │ nazg-agent                 │     │
│  │  - docker scanner          │     │
│  │  - periodic status collect │     │
│  │  - phone home to nazg      │     │
│  └──────────┬─────────────────┘     │
└─────────────┼───────────────────────┘
              │ status updates
              v
┌─────────────────────────────────────┐
│  Control Machine (nazg)             │
│  ┌────────────────────────────┐     │
│  │ controlCenter listener     │     │
│  │  - receives agent updates  │     │
│  │  - validates/stores        │     │
│  └──────────┬─────────────────┘     │
│             v                        │
│  ┌────────────────────────────┐     │
│  │ nexus (SQLite)             │     │
│  │  - servers                 │     │
│  │  - containers              │     │
│  │  - compose_files           │     │
│  │  - docker_status_history   │     │
│  └────────────────────────────┘     │
└─────────────────────────────────────┘
```

## Database Schema

### servers
Tracks remote hosts running docker.

```sql
CREATE TABLE servers (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  label TEXT NOT NULL UNIQUE,          -- e.g., "media-server", "tank"
  host TEXT NOT NULL,                  -- e.g., "tank@10.0.0.4"
  agent_version TEXT,                  -- e.g., "0.1.0"
  agent_status TEXT DEFAULT 'unknown', -- online, offline, error
  last_heartbeat INTEGER,              -- Unix timestamp
  capabilities TEXT,                   -- JSON array: ["docker", "compose", "systemd"]
  ssh_config TEXT,                     -- JSON: {"key": "/path", "port": 22}
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
```

### containers
Current state of all containers across all servers.

```sql
CREATE TABLE containers (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  server_id INTEGER NOT NULL,
  container_id TEXT NOT NULL,          -- Docker container ID (short or full)
  name TEXT NOT NULL,                  -- Container name (e.g., "plex")
  image TEXT NOT NULL,                 -- Image name:tag
  state TEXT NOT NULL,                 -- running, exited, paused, restarting
  status TEXT,                         -- e.g., "Up 3 days", "Exited (0) 2 hours ago"
  created INTEGER,                     -- Unix timestamp when container was created
  ports TEXT,                          -- JSON: [{"host": 32400, "container": 32400}]
  volumes TEXT,                        -- JSON: ["/host/path:/container/path"]
  networks TEXT,                       -- JSON: ["bridge", "media-net"]
  compose_file_id INTEGER,             -- Reference to compose file if created by compose
  service_name TEXT,                   -- Service name in compose file
  depends_on TEXT,                     -- JSON: ["service1", "service2"]
  labels TEXT,                         -- JSON: {"key": "value"}
  env_vars TEXT,                       -- JSON: {"KEY": "value"} (non-sensitive only)
  health_status TEXT,                  -- healthy, unhealthy, starting, none
  restart_policy TEXT,                 -- no, always, on-failure, unless-stopped
  last_seen INTEGER NOT NULL,          -- Unix timestamp of last scan
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
  FOREIGN KEY (compose_file_id) REFERENCES compose_files(id) ON DELETE SET NULL,
  UNIQUE(server_id, container_id)
);

CREATE INDEX idx_containers_server_id ON containers(server_id);
CREATE INDEX idx_containers_name ON containers(name);
CREATE INDEX idx_containers_state ON containers(state);
```

### compose_files
Track docker-compose files and their services.

```sql
CREATE TABLE compose_files (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  server_id INTEGER NOT NULL,
  file_path TEXT NOT NULL,             -- Absolute path on remote server
  project_name TEXT,                   -- Docker Compose project name
  services TEXT NOT NULL,              -- JSON array of service definitions
  networks TEXT,                       -- JSON: network definitions
  volumes TEXT,                        -- JSON: volume definitions
  file_hash TEXT,                      -- SHA256 of compose file content
  last_scan INTEGER NOT NULL,          -- Unix timestamp of last scan
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
  UNIQUE(server_id, file_path)
);

CREATE INDEX idx_compose_files_server_id ON compose_files(server_id);
```

### docker_images
Track images available on each server.

```sql
CREATE TABLE docker_images (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  server_id INTEGER NOT NULL,
  image_id TEXT NOT NULL,              -- Docker image ID
  repository TEXT,                     -- e.g., "linuxserver/plex"
  tag TEXT,                            -- e.g., "latest"
  size_bytes INTEGER,                  -- Image size in bytes
  created INTEGER,                     -- Unix timestamp
  last_seen INTEGER NOT NULL,          -- Unix timestamp of last scan
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
  UNIQUE(server_id, image_id)
);

CREATE INDEX idx_docker_images_server_id ON docker_images(server_id);
```

### docker_networks
Track docker networks on each server.

```sql
CREATE TABLE docker_networks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  server_id INTEGER NOT NULL,
  network_id TEXT NOT NULL,            -- Docker network ID
  name TEXT NOT NULL,                  -- Network name
  driver TEXT,                         -- bridge, host, overlay, etc.
  scope TEXT,                          -- local, global, swarm
  ipam_config TEXT,                    -- JSON: IP allocation config
  last_seen INTEGER NOT NULL,          -- Unix timestamp of last scan
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
  UNIQUE(server_id, network_id)
);

CREATE INDEX idx_docker_networks_server_id ON docker_networks(server_id);
```

### docker_volumes
Track docker volumes on each server.

```sql
CREATE TABLE docker_volumes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  server_id INTEGER NOT NULL,
  volume_name TEXT NOT NULL,           -- Volume name
  driver TEXT,                         -- local, nfs, etc.
  mountpoint TEXT,                     -- Path on host
  labels TEXT,                         -- JSON: {"key": "value"}
  last_seen INTEGER NOT NULL,          -- Unix timestamp of last scan
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
  UNIQUE(server_id, volume_name)
);

CREATE INDEX idx_docker_volumes_server_id ON docker_volumes(server_id);
```

### docker_status_history
Timeline of container state changes.

```sql
CREATE TABLE docker_status_history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  server_id INTEGER NOT NULL,
  container_id TEXT NOT NULL,          -- References containers.container_id
  event_type TEXT NOT NULL,            -- start, stop, restart, die, health_status, create, destroy
  old_state TEXT,                      -- Previous state
  new_state TEXT NOT NULL,             -- New state
  metadata TEXT,                       -- JSON: additional event data
  timestamp INTEGER NOT NULL,          -- Unix timestamp
  FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE
);

CREATE INDEX idx_docker_status_history_server_id ON docker_status_history(server_id);
CREATE INDEX idx_docker_status_history_container_id ON docker_status_history(container_id);
CREATE INDEX idx_docker_status_history_timestamp ON docker_status_history(timestamp);
```

### agent_updates
Log of all status updates received from agents (audit trail).

```sql
CREATE TABLE agent_updates (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  server_id INTEGER NOT NULL,
  update_type TEXT NOT NULL,           -- heartbeat, full_scan, container_event
  payload_json TEXT NOT NULL,          -- Full JSON payload received
  processed BOOLEAN DEFAULT 0,         -- Whether update was successfully processed
  error_message TEXT,                  -- Error if processing failed
  timestamp INTEGER NOT NULL,          -- Unix timestamp
  FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE
);

CREATE INDEX idx_agent_updates_server_id ON agent_updates(server_id);
CREATE INDEX idx_agent_updates_timestamp ON agent_updates(timestamp);
```

## Protocol Messages

### Agent → Nazg: Heartbeat
```json
{
  "type": "heartbeat",
  "server_label": "media-server",
  "agent_version": "0.1.0",
  "timestamp": 1698765432,
  "capabilities": ["docker", "compose", "systemd"],
  "system_info": {
    "hostname": "tank",
    "os": "Ubuntu 22.04",
    "docker_version": "24.0.5"
  }
}
```

### Agent → Nazg: Full Scan
```json
{
  "type": "full_scan",
  "server_label": "media-server",
  "timestamp": 1698765432,
  "containers": [
    {
      "id": "abc123",
      "name": "plex",
      "image": "linuxserver/plex:latest",
      "state": "running",
      "status": "Up 3 days",
      "created": 1698000000,
      "ports": [{"host": 32400, "container": 32400}],
      "volumes": ["/tank/media:/data:ro"],
      "networks": ["media-net"],
      "compose_file": "/opt/media/docker-compose.yml",
      "service_name": "plex",
      "health_status": "healthy",
      "restart_policy": "unless-stopped"
    }
  ],
  "compose_files": [
    {
      "path": "/opt/media/docker-compose.yml",
      "project_name": "media",
      "services": ["plex", "sonarr", "radarr"],
      "hash": "sha256:abc123..."
    }
  ],
  "images": [
    {
      "id": "img123",
      "repository": "linuxserver/plex",
      "tag": "latest",
      "size": 1073741824,
      "created": 1698000000
    }
  ],
  "networks": [...],
  "volumes": [...]
}
```

### Agent → Nazg: Container Event
```json
{
  "type": "container_event",
  "server_label": "media-server",
  "timestamp": 1698765432,
  "container_id": "abc123",
  "container_name": "plex",
  "event": "restart",
  "old_state": "running",
  "new_state": "running",
  "metadata": {
    "reason": "manual",
    "exit_code": 0
  }
}
```

## Implementation Phases

### Phase 1: Database Foundation
- Add migration to nexus for docker monitoring tables
- Add C++ API methods to nexus::Store for docker operations
- Test with manual SQL inserts

### Phase 2: Agent Scanner
- Implement docker scanner in agent (executes docker commands)
- Parse docker output into structured data
- Discover compose files in common locations

### Phase 3: Phone Home
- Modify agent to connect to nazg (reverse of current model)
- Implement protocol encoding/decoding for status messages
- Add controlCenter listener in nazg to receive connections

### Phase 4: Status Processing
- Parse agent updates in nazg
- Store in nexus tables
- Update server heartbeat timestamps

### Phase 5: CLI Monitoring
- `nazg server list` - show all servers and their status
- `nazg docker list [--server name]` - show all containers
- `nazg docker status <container>` - show specific container details
- `nazg docker history <container>` - show state change timeline

## Configuration

### Agent Config (on remote server)
```toml
[agent]
server_label = "media-server"
control_center_host = "nazg-controller.local"
control_center_port = 7070
heartbeat_interval_sec = 30
scan_interval_sec = 300

[docker]
enabled = true
scan_compose_paths = ["/opt", "/srv", "/home/*/docker"]
include_env_vars = false  # Security: don't send env vars
```

### Nazg Config (control machine)
```toml
[controlCenter]
enabled = true
bind_address = "0.0.0.0"
port = 7070
require_auth = true
auth_token = "secret-token-here"

[docker]
retention_history_days = 90
```

## Security Considerations

- Agent authenticates with shared token (later: mutual TLS)
- Don't transmit sensitive env vars or secrets
- Rate limiting on agent updates
- Validate all JSON payloads before processing
- Store audit trail of all updates

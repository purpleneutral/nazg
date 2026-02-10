-- Docker monitoring schema
-- Version: 9
-- Extends servers and adds container/compose tracking for remote docker environments

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

-- Servers table (enhanced from bot_hosts to be more general-purpose)
-- Tracks remote hosts that can run docker, agents, or other services
CREATE TABLE IF NOT EXISTS servers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    label TEXT NOT NULL UNIQUE,          -- e.g., "media-server", "tank"
    host TEXT NOT NULL,                  -- e.g., "tank@10.0.0.4" or "10.0.0.4"
    agent_version TEXT,                  -- Version of nazg-agent installed
    agent_status TEXT DEFAULT 'unknown', -- online, offline, error
    last_heartbeat INTEGER,              -- Unix timestamp of last agent check-in
    capabilities TEXT,                   -- JSON array: ["docker", "compose", "systemd"]
    ssh_config TEXT,                     -- JSON: {"key": "/path", "port": 22}
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_servers_label ON servers(label);
CREATE INDEX IF NOT EXISTS idx_servers_agent_status ON servers(agent_status);
CREATE INDEX IF NOT EXISTS idx_servers_last_heartbeat ON servers(last_heartbeat);

-- Containers table (current state of docker containers)
CREATE TABLE IF NOT EXISTS containers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    container_id TEXT NOT NULL,          -- Docker container ID (12-char short form)
    name TEXT NOT NULL,                  -- Container name (e.g., "plex")
    image TEXT NOT NULL,                 -- Image name:tag
    state TEXT NOT NULL,                 -- running, exited, paused, restarting, dead
    status TEXT,                         -- e.g., "Up 3 days", "Exited (0) 2 hours ago"
    created INTEGER,                     -- Unix timestamp when container was created
    ports TEXT,                          -- JSON: [{"host": 32400, "container": 32400, "proto": "tcp"}]
    volumes TEXT,                        -- JSON: [{"host": "/tank/media", "container": "/data", "mode": "ro"}]
    networks TEXT,                       -- JSON: ["bridge", "media-net"]
    compose_file_id INTEGER,             -- Reference to compose file if created by compose
    service_name TEXT,                   -- Service name in compose file
    depends_on TEXT,                     -- JSON: ["service1", "service2"]
    labels TEXT,                         -- JSON: {"key": "value"}
    env_vars TEXT,                       -- JSON: {"KEY": "value"} (non-sensitive only)
    health_status TEXT,                  -- healthy, unhealthy, starting, none
    restart_policy TEXT,                 -- no, always, on-failure, unless-stopped
    last_seen INTEGER NOT NULL,          -- Unix timestamp of last scan/update
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    FOREIGN KEY (compose_file_id) REFERENCES compose_files(id) ON DELETE SET NULL,
    UNIQUE(server_id, container_id)
);

CREATE INDEX IF NOT EXISTS idx_containers_server_id ON containers(server_id);
CREATE INDEX IF NOT EXISTS idx_containers_name ON containers(name);
CREATE INDEX IF NOT EXISTS idx_containers_state ON containers(state);
CREATE INDEX IF NOT EXISTS idx_containers_service_name ON containers(service_name);
CREATE INDEX IF NOT EXISTS idx_containers_last_seen ON containers(last_seen);

-- Compose files table (docker-compose.yml tracking)
CREATE TABLE IF NOT EXISTS compose_files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    file_path TEXT NOT NULL,             -- Absolute path on remote server
    project_name TEXT,                   -- Docker Compose project name
    services TEXT NOT NULL,              -- JSON array of service names: ["plex", "sonarr"]
    networks TEXT,                       -- JSON: network definitions from compose file
    volumes TEXT,                        -- JSON: volume definitions from compose file
    file_hash TEXT,                      -- SHA256 of compose file content
    last_scan INTEGER NOT NULL,          -- Unix timestamp of last scan
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    UNIQUE(server_id, file_path)
);

CREATE INDEX IF NOT EXISTS idx_compose_files_server_id ON compose_files(server_id);
CREATE INDEX IF NOT EXISTS idx_compose_files_project_name ON compose_files(project_name);

-- Docker images table
CREATE TABLE IF NOT EXISTS docker_images (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    image_id TEXT NOT NULL,              -- Docker image ID (12-char short form)
    repository TEXT,                     -- e.g., "linuxserver/plex"
    tag TEXT,                            -- e.g., "latest"
    size_bytes INTEGER,                  -- Image size in bytes
    created INTEGER,                     -- Unix timestamp when image was built
    last_seen INTEGER NOT NULL,          -- Unix timestamp of last scan
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    UNIQUE(server_id, image_id)
);

CREATE INDEX IF NOT EXISTS idx_docker_images_server_id ON docker_images(server_id);
CREATE INDEX IF NOT EXISTS idx_docker_images_repository ON docker_images(repository);

-- Docker networks table
CREATE TABLE IF NOT EXISTS docker_networks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    network_id TEXT NOT NULL,            -- Docker network ID
    name TEXT NOT NULL,                  -- Network name
    driver TEXT,                         -- bridge, host, overlay, macvlan, etc.
    scope TEXT,                          -- local, global, swarm
    ipam_config TEXT,                    -- JSON: IP allocation config
    last_seen INTEGER NOT NULL,          -- Unix timestamp of last scan
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    UNIQUE(server_id, network_id)
);

CREATE INDEX IF NOT EXISTS idx_docker_networks_server_id ON docker_networks(server_id);
CREATE INDEX IF NOT EXISTS idx_docker_networks_name ON docker_networks(name);

-- Docker volumes table
CREATE TABLE IF NOT EXISTS docker_volumes (
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

CREATE INDEX IF NOT EXISTS idx_docker_volumes_server_id ON docker_volumes(server_id);
CREATE INDEX IF NOT EXISTS idx_docker_volumes_name ON docker_volumes(volume_name);

-- Docker status history (timeline of container events)
CREATE TABLE IF NOT EXISTS docker_status_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    container_id TEXT NOT NULL,          -- Docker container ID (matches containers.container_id)
    container_name TEXT,                 -- Denormalized for easier querying
    event_type TEXT NOT NULL,            -- start, stop, restart, die, health_status, create, destroy, pause, unpause
    old_state TEXT,                      -- Previous state (can be NULL for create events)
    new_state TEXT NOT NULL,             -- New state
    metadata TEXT,                       -- JSON: additional event data (exit_code, signal, etc.)
    timestamp INTEGER NOT NULL,          -- Unix timestamp of event
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_docker_status_history_server_id ON docker_status_history(server_id);
CREATE INDEX IF NOT EXISTS idx_docker_status_history_container_id ON docker_status_history(container_id);
CREATE INDEX IF NOT EXISTS idx_docker_status_history_container_name ON docker_status_history(container_name);
CREATE INDEX IF NOT EXISTS idx_docker_status_history_timestamp ON docker_status_history(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_docker_status_history_event_type ON docker_status_history(event_type);

-- Agent updates table (audit trail of all status updates from agents)
CREATE TABLE IF NOT EXISTS agent_updates (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    update_type TEXT NOT NULL,           -- heartbeat, full_scan, container_event, incremental_update
    payload_json TEXT NOT NULL,          -- Full JSON payload received from agent
    processed BOOLEAN DEFAULT 0,         -- Whether update was successfully processed
    error_message TEXT,                  -- Error message if processing failed
    timestamp INTEGER NOT NULL,          -- Unix timestamp when update was received
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_agent_updates_server_id ON agent_updates(server_id);
CREATE INDEX IF NOT EXISTS idx_agent_updates_timestamp ON agent_updates(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_agent_updates_update_type ON agent_updates(update_type);
CREATE INDEX IF NOT EXISTS idx_agent_updates_processed ON agent_updates(processed);

-- Docker commands table (history of control commands issued by nazg)
-- This will be used in phase 2 when we add control operations
CREATE TABLE IF NOT EXISTS docker_commands (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    container_id TEXT,                   -- Target container (NULL for server-level commands)
    command TEXT NOT NULL,               -- restart, stop, start, logs, exec, etc.
    params TEXT,                         -- JSON: command parameters
    status TEXT NOT NULL,                -- pending, running, success, failed
    exit_code INTEGER,
    output TEXT,                         -- Command output
    error TEXT,                          -- Error message if failed
    issued_by TEXT,                      -- User or system that issued command
    issued_at INTEGER NOT NULL,          -- Unix timestamp
    completed_at INTEGER,                -- Unix timestamp when completed
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_docker_commands_server_id ON docker_commands(server_id);
CREATE INDEX IF NOT EXISTS idx_docker_commands_container_id ON docker_commands(container_id);
CREATE INDEX IF NOT EXISTS idx_docker_commands_status ON docker_commands(status);
CREATE INDEX IF NOT EXISTS idx_docker_commands_issued_at ON docker_commands(issued_at DESC);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (9, strftime('%s', 'now'));

COMMIT;

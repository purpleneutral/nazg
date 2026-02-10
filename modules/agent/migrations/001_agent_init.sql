-- Agent local cache schema
-- Version: 1
-- Stores Docker environment snapshots for phone-home reporting

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

-- Schema version tracking
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at INTEGER NOT NULL
);

-- Agent configuration
CREATE TABLE IF NOT EXISTS agent_config (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);

-- Insert default config
INSERT OR IGNORE INTO agent_config (key, value, updated_at)
VALUES ('server_label', 'unknown', strftime('%s', 'now'));

INSERT OR IGNORE INTO agent_config (key, value, updated_at)
VALUES ('last_scan', '0', strftime('%s', 'now'));

INSERT OR IGNORE INTO agent_config (key, value, updated_at)
VALUES ('control_center_url', '', strftime('%s', 'now'));

-- Cached container snapshots
CREATE TABLE IF NOT EXISTS cached_containers (
    container_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    image TEXT NOT NULL,
    state TEXT NOT NULL,
    status TEXT,
    created INTEGER,
    service_name TEXT,
    health_status TEXT,
    restart_policy TEXT,
    labels_json TEXT,
    last_seen INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_cached_containers_state ON cached_containers(state);
CREATE INDEX IF NOT EXISTS idx_cached_containers_last_seen ON cached_containers(last_seen);

-- Cached compose file info
CREATE TABLE IF NOT EXISTS cached_compose_files (
    path TEXT PRIMARY KEY,
    project_name TEXT,
    services_json TEXT NOT NULL,
    file_hash TEXT,
    last_seen INTEGER NOT NULL
);

-- Cached Docker images
CREATE TABLE IF NOT EXISTS cached_images (
    image_id TEXT PRIMARY KEY,
    repository TEXT,
    tag TEXT,
    size_bytes INTEGER,
    created INTEGER,
    last_seen INTEGER NOT NULL
);

-- Cached Docker networks
CREATE TABLE IF NOT EXISTS cached_networks (
    network_id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    driver TEXT,
    scope TEXT,
    last_seen INTEGER NOT NULL
);

-- Cached Docker volumes
CREATE TABLE IF NOT EXISTS cached_volumes (
    volume_name TEXT PRIMARY KEY,
    driver TEXT,
    mountpoint TEXT,
    last_seen INTEGER NOT NULL
);

-- Scan history (track when scans were performed and sent)
CREATE TABLE IF NOT EXISTS scan_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    scan_timestamp INTEGER NOT NULL,
    containers_count INTEGER DEFAULT 0,
    images_count INTEGER DEFAULT 0,
    networks_count INTEGER DEFAULT 0,
    volumes_count INTEGER DEFAULT 0,
    compose_files_count INTEGER DEFAULT 0,
    sent_to_control BOOLEAN DEFAULT 0,
    sent_at INTEGER
);

CREATE INDEX IF NOT EXISTS idx_scan_history_timestamp ON scan_history(scan_timestamp DESC);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (1, strftime('%s', 'now'));

COMMIT;

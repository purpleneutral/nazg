-- Initial schema for Nexus
-- Version: 1

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

-- Schema version tracking
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at INTEGER NOT NULL
);

-- Projects table
CREATE TABLE IF NOT EXISTS projects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    root_path TEXT UNIQUE NOT NULL,
    name TEXT,
    language TEXT,
    detected_tools TEXT,  -- JSON array: ["cmake", "git", "docker"]
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_projects_root_path ON projects(root_path);

-- Snapshots (file tree hashes)
CREATE TABLE IF NOT EXISTS snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    tree_hash TEXT NOT NULL,
    file_count INTEGER,
    total_bytes INTEGER,
    created_at INTEGER NOT NULL,
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_snapshots_project_id ON snapshots(project_id);
CREATE INDEX IF NOT EXISTS idx_snapshots_created_at ON snapshots(created_at);

-- Events (breadcrumb trail)
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    level TEXT NOT NULL,  -- info, warn, error
    tag TEXT NOT NULL,    -- scanner, detector, planner, etc.
    message TEXT NOT NULL,
    metadata TEXT,        -- JSON for extra context
    created_at INTEGER NOT NULL,
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_events_project_id ON events(project_id);
CREATE INDEX IF NOT EXISTS idx_events_created_at ON events(created_at);
CREATE INDEX IF NOT EXISTS idx_events_level ON events(level);

-- Command history
CREATE TABLE IF NOT EXISTS command_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER,
    command TEXT NOT NULL,
    args TEXT,           -- JSON array
    exit_code INTEGER,
    duration_ms INTEGER,
    executed_at INTEGER NOT NULL,
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_command_history_executed_at ON command_history(executed_at);
CREATE INDEX IF NOT EXISTS idx_command_history_project_id ON command_history(project_id);

-- Facts (persistent key-value per project)
CREATE TABLE IF NOT EXISTS facts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    namespace TEXT NOT NULL,  -- e.g., "detector", "config", "brain"
    key TEXT NOT NULL,
    value TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE,
    UNIQUE(project_id, namespace, key)
);

CREATE INDEX IF NOT EXISTS idx_facts_project_id ON facts(project_id);
CREATE INDEX IF NOT EXISTS idx_facts_namespace ON facts(namespace);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (1, strftime('%s', 'now'));

COMMIT;

-- Workspace Time Machine schema
-- Version: 5

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

-- Workspace snapshots (extends brain snapshots)
CREATE TABLE IF NOT EXISTS workspace_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    brain_snapshot_id INTEGER,
    label TEXT,
    trigger_type TEXT NOT NULL,  -- "auto", "manual", "pre-build", "pre-upgrade"
    timestamp INTEGER NOT NULL,

    -- Extended state capture
    build_dir_hash TEXT,
    deps_manifest_hash TEXT,
    env_snapshot TEXT,           -- JSON of relevant env vars
    system_info TEXT,            -- Compiler versions, system libs (JSON)

    -- Metadata
    restore_count INTEGER DEFAULT 0,
    is_clean_build INTEGER DEFAULT 0,
    git_commit TEXT,
    git_branch TEXT,

    created_at INTEGER NOT NULL,
    tags TEXT,                   -- Comma-separated user tags

    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY(brain_snapshot_id) REFERENCES snapshots(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_workspace_snapshots_project_id
    ON workspace_snapshots(project_id);
CREATE INDEX IF NOT EXISTS idx_workspace_snapshots_timestamp
    ON workspace_snapshots(timestamp);
CREATE INDEX IF NOT EXISTS idx_workspace_snapshots_trigger_type
    ON workspace_snapshots(trigger_type);

-- Snapshot file manifest (what's in each snapshot)
CREATE TABLE IF NOT EXISTS workspace_files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id INTEGER NOT NULL,
    file_path TEXT NOT NULL,
    file_type TEXT NOT NULL,     -- "source", "build", "dep", "config"
    file_hash TEXT NOT NULL,
    file_size INTEGER,
    mtime INTEGER,

    FOREIGN KEY(snapshot_id) REFERENCES workspace_snapshots(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_workspace_files_snapshot_id
    ON workspace_files(snapshot_id);
CREATE INDEX IF NOT EXISTS idx_workspace_files_path
    ON workspace_files(file_path);
CREATE INDEX IF NOT EXISTS idx_workspace_files_type
    ON workspace_files(file_type);

-- Restore history (track what was restored when)
CREATE TABLE IF NOT EXISTS workspace_restores (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    from_snapshot_id INTEGER NOT NULL,
    restore_type TEXT NOT NULL,  -- "full", "smart", "partial"
    files_restored INTEGER,
    timestamp INTEGER NOT NULL,
    reason TEXT,
    success INTEGER DEFAULT 0,
    duration_ms INTEGER,

    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY(from_snapshot_id) REFERENCES workspace_snapshots(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_workspace_restores_project_id
    ON workspace_restores(project_id);
CREATE INDEX IF NOT EXISTS idx_workspace_restores_timestamp
    ON workspace_restores(timestamp);

-- Failure patterns (learn what changes cause failures)
CREATE TABLE IF NOT EXISTS workspace_failures (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    failure_type TEXT NOT NULL,  -- "build", "test", "runtime"
    error_signature TEXT NOT NULL,
    error_message TEXT,

    -- State before failure
    before_snapshot_id INTEGER,
    after_snapshot_id INTEGER,

    -- What changed
    changed_files TEXT,          -- JSON array of file paths
    changed_deps TEXT,           -- JSON of dependency changes

    -- Resolution (if known)
    resolved INTEGER DEFAULT 0,
    resolution_type TEXT,        -- "restore", "fix", "upgrade"
    resolution_notes TEXT,

    timestamp INTEGER NOT NULL,

    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY(before_snapshot_id) REFERENCES workspace_snapshots(id) ON DELETE SET NULL,
    FOREIGN KEY(after_snapshot_id) REFERENCES workspace_snapshots(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_workspace_failures_project_id
    ON workspace_failures(project_id);
CREATE INDEX IF NOT EXISTS idx_workspace_failures_signature
    ON workspace_failures(error_signature);
CREATE INDEX IF NOT EXISTS idx_workspace_failures_resolved
    ON workspace_failures(resolved);

-- Workspace tags (named checkpoints)
CREATE TABLE IF NOT EXISTS workspace_tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    snapshot_id INTEGER NOT NULL,
    tag_name TEXT NOT NULL,
    description TEXT,
    created_at INTEGER NOT NULL,

    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY(snapshot_id) REFERENCES workspace_snapshots(id) ON DELETE CASCADE,
    UNIQUE(project_id, tag_name)
);

CREATE INDEX IF NOT EXISTS idx_workspace_tags_project_id
    ON workspace_tags(project_id);
CREATE INDEX IF NOT EXISTS idx_workspace_tags_tag_name
    ON workspace_tags(tag_name);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (5, strftime('%s', 'now'));

COMMIT;

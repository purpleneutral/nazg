-- Git server migrations and health tracking
PRAGMA foreign_keys=ON;
BEGIN TRANSACTION;

CREATE TABLE IF NOT EXISTS repo_migrations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER REFERENCES git_servers(id) ON DELETE CASCADE,
    project_id INTEGER REFERENCES projects(id),
    repo_name TEXT NOT NULL,
    source_path TEXT,
    started_at INTEGER NOT NULL,
    completed_at INTEGER,
    status TEXT DEFAULT 'pending',
    branch_count INTEGER,
    tag_count INTEGER,
    size_bytes INTEGER,
    error_message TEXT
);

CREATE INDEX IF NOT EXISTS idx_repo_migrations_server
    ON repo_migrations(server_id);
CREATE INDEX IF NOT EXISTS idx_repo_migrations_project
    ON repo_migrations(project_id);

CREATE TABLE IF NOT EXISTS git_server_health (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL REFERENCES git_servers(id) ON DELETE CASCADE,
    timestamp INTEGER NOT NULL,
    status TEXT NOT NULL, -- ok, warning, critical
    web_ui_reachable INTEGER,
    http_clone_works INTEGER,
    ssh_push_works INTEGER,
    service_status TEXT,
    repo_count INTEGER,
    total_size_bytes INTEGER,
    disk_used_pct INTEGER,
    disk_free_bytes INTEGER,
    notes TEXT
);

CREATE INDEX IF NOT EXISTS idx_git_server_health_server
    ON git_server_health(server_id, timestamp DESC);

-- For forward compatibility if bare_repos lacked indexes
CREATE INDEX IF NOT EXISTS idx_bare_repos_name
    ON bare_repos(name);

INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (8, strftime('%s', 'now'));

COMMIT;

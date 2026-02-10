-- Bot system schema
-- Version: 3

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

-- Bot hosts (remote machines where bots execute)
CREATE TABLE IF NOT EXISTS bot_hosts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    label TEXT UNIQUE NOT NULL,       -- e.g., "lab-box", "prod-server"
    address TEXT NOT NULL,             -- e.g., "user@host" or "host"
    ssh_config TEXT,                   -- JSON: {"key": "~/.ssh/key", "port": 22, "options": []}
    last_run_at INTEGER,               -- Unix timestamp of last bot execution
    last_status TEXT,                  -- "ok", "warning", "critical", "error"
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_bot_hosts_label ON bot_hosts(label);
CREATE INDEX IF NOT EXISTS idx_bot_hosts_last_run_at ON bot_hosts(last_run_at);

-- Bot runs (individual bot executions)
CREATE TABLE IF NOT EXISTS bot_runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    bot_name TEXT NOT NULL,            -- e.g., "doctor", "build", "backup"
    host_id INTEGER NOT NULL,
    started_at INTEGER NOT NULL,
    finished_at INTEGER,
    status TEXT NOT NULL,              -- "running", "success", "warning", "error"
    exit_code INTEGER,
    duration_ms INTEGER,
    FOREIGN KEY(host_id) REFERENCES bot_hosts(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_bot_runs_bot_name ON bot_runs(bot_name);
CREATE INDEX IF NOT EXISTS idx_bot_runs_host_id ON bot_runs(host_id);
CREATE INDEX IF NOT EXISTS idx_bot_runs_started_at ON bot_runs(started_at);
CREATE INDEX IF NOT EXISTS idx_bot_runs_status ON bot_runs(status);

-- Bot reports (diagnostic data from bot executions)
CREATE TABLE IF NOT EXISTS bot_reports (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    payload_json TEXT NOT NULL,        -- Full JSON report from bot
    created_at INTEGER NOT NULL,
    FOREIGN KEY(run_id) REFERENCES bot_runs(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_bot_reports_run_id ON bot_reports(run_id);
CREATE INDEX IF NOT EXISTS idx_bot_reports_created_at ON bot_reports(created_at);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (3, strftime('%s', 'now'));

COMMIT;

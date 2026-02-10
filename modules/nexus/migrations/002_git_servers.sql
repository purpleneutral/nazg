-- Git server and repository management
PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;

-- Git servers (cgit, gitea, gitlab, etc.)
CREATE TABLE IF NOT EXISTS git_servers (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  label TEXT NOT NULL UNIQUE,   -- 'myserver', 'production', etc.
  type TEXT NOT NULL,            -- 'cgit', 'gitea', 'gitlab'
  host TEXT NOT NULL,            -- '10.0.0.4'
  port INTEGER DEFAULT 80,
  ssh_port INTEGER DEFAULT 22,
  ssh_user TEXT,                 -- 'git' or 'director'
  repo_base_path TEXT,           -- '/srv/git' on server
  config_path TEXT,              -- '/etc/cgitrc'
  web_url TEXT,                  -- 'http://10.0.0.4/cgit'

  -- Runtime state
  status TEXT,                   -- 'online', 'offline', 'not_installed', 'installed'
  installed_at INTEGER,          -- Timestamp of installation
  last_check INTEGER,            -- Last health check

  -- Config tracking (for conflict detection)
  config_hash TEXT,              -- Hash of config.toml values
  config_modified INTEGER,       -- When config.toml was last different

  created_at INTEGER NOT NULL,
  updated_at INTEGER
);

-- Bare repositories
CREATE TABLE IF NOT EXISTS bare_repos (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER REFERENCES projects(id),
  name TEXT NOT NULL,
  local_path TEXT NOT NULL,     -- '/mnt/nas/repos/myproject.git'
  server_path TEXT,             -- '/srv/git/myproject.git' on server
  server_id INTEGER REFERENCES git_servers(id),
  is_synced BOOLEAN DEFAULT 0,
  last_sync INTEGER,
  created_at INTEGER NOT NULL
);

-- Git remotes
CREATE TABLE IF NOT EXISTS git_remotes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  bare_repo_id INTEGER REFERENCES bare_repos(id),
  name TEXT NOT NULL,           -- 'origin', 'server', 'backup'
  url TEXT NOT NULL,
  remote_type TEXT,             -- 'local-bare', 'server', 'github'
  priority INTEGER DEFAULT 0,   -- Push order (lower = first)
  auto_sync BOOLEAN DEFAULT 1,
  last_push INTEGER
);

-- Indexes
CREATE INDEX IF NOT EXISTS idx_bare_repos_project ON bare_repos(project_id);
CREATE INDEX IF NOT EXISTS idx_bare_repos_server ON bare_repos(server_id);
CREATE INDEX IF NOT EXISTS idx_git_remotes_bare ON git_remotes(bare_repo_id);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (2, strftime('%s', 'now'));

COMMIT;
PRAGMA foreign_keys=ON;

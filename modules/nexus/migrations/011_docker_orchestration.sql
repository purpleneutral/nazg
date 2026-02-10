-- Docker orchestration and stack management
-- Version: 11
-- Adds support for intelligent stack orchestration, dependency tracking, and rules

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

-- Stack profiles: group related compose files and services
CREATE TABLE IF NOT EXISTS docker_stacks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    name TEXT NOT NULL,                      -- e.g., "vpn-stack", "media-stack"
    description TEXT,
    priority INTEGER DEFAULT 0,              -- Higher priority stacks start first
    auto_restart BOOLEAN DEFAULT 0,          -- Auto-restart on failure
    health_check_timeout INTEGER DEFAULT 30, -- Seconds to wait for health
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    UNIQUE(server_id, name)
);

CREATE INDEX IF NOT EXISTS idx_docker_stacks_server_id ON docker_stacks(server_id);
CREATE INDEX IF NOT EXISTS idx_docker_stacks_priority ON docker_stacks(priority DESC);

-- Map compose files to stacks
CREATE TABLE IF NOT EXISTS docker_stack_compose_files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    stack_id INTEGER NOT NULL,
    compose_file_id INTEGER NOT NULL,
    execution_order INTEGER DEFAULT 0,       -- Order within the stack
    env_file TEXT,                           -- Path to env file if needed
    compose_flags TEXT,                      -- Additional flags (JSON array)
    FOREIGN KEY (stack_id) REFERENCES docker_stacks(id) ON DELETE CASCADE,
    FOREIGN KEY (compose_file_id) REFERENCES compose_files(id) ON DELETE CASCADE,
    UNIQUE(stack_id, compose_file_id)
);

CREATE INDEX IF NOT EXISTS idx_stack_compose_stack_id ON docker_stack_compose_files(stack_id);
CREATE INDEX IF NOT EXISTS idx_stack_compose_order ON docker_stack_compose_files(execution_order);

-- Service dependencies: track dependencies between services
CREATE TABLE IF NOT EXISTS docker_service_dependencies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    service_name TEXT NOT NULL,              -- The service (e.g., "transmission")
    depends_on_service TEXT NOT NULL,        -- Service it depends on (e.g., "vpn")
    dependency_type TEXT DEFAULT 'soft',     -- soft, hard, network, volume
    compose_file_id INTEGER,                 -- Source compose file
    created_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    FOREIGN KEY (compose_file_id) REFERENCES compose_files(id) ON DELETE SET NULL,
    UNIQUE(server_id, service_name, depends_on_service)
);

CREATE INDEX IF NOT EXISTS idx_service_deps_server_id ON docker_service_dependencies(server_id);
CREATE INDEX IF NOT EXISTS idx_service_deps_service ON docker_service_dependencies(service_name);
CREATE INDEX IF NOT EXISTS idx_service_deps_depends_on ON docker_service_dependencies(depends_on_service);

-- Network dependencies: track which services use which networks
CREATE TABLE IF NOT EXISTS docker_network_dependencies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    service_name TEXT NOT NULL,
    network_name TEXT NOT NULL,
    static_ip TEXT,                          -- Static IP if configured
    created_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    UNIQUE(server_id, service_name, network_name)
);

CREATE INDEX IF NOT EXISTS idx_network_deps_server_id ON docker_network_dependencies(server_id);
CREATE INDEX IF NOT EXISTS idx_network_deps_network ON docker_network_dependencies(network_name);

-- Orchestration rules: user-defined rules for stack management
CREATE TABLE IF NOT EXISTS docker_orchestration_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    stack_id INTEGER,                        -- NULL = applies to all stacks
    rule_type TEXT NOT NULL,                 -- restart_order, health_check, dependency, custom
    condition_json TEXT NOT NULL,            -- JSON: conditions to trigger rule
    action_json TEXT NOT NULL,               -- JSON: actions to take
    priority INTEGER DEFAULT 0,              -- Higher priority rules execute first
    enabled BOOLEAN DEFAULT 1,
    description TEXT,
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    FOREIGN KEY (stack_id) REFERENCES docker_stacks(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_orch_rules_server_id ON docker_orchestration_rules(server_id);
CREATE INDEX IF NOT EXISTS idx_orch_rules_stack_id ON docker_orchestration_rules(stack_id);
CREATE INDEX IF NOT EXISTS idx_orch_rules_priority ON docker_orchestration_rules(priority DESC);
CREATE INDEX IF NOT EXISTS idx_orch_rules_enabled ON docker_orchestration_rules(enabled);

-- Issues detected by agent: phone-home reporting
CREATE TABLE IF NOT EXISTS docker_issues (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    container_id TEXT,                       -- Affected container (can be NULL for server-level issues)
    issue_type TEXT NOT NULL,                -- unhealthy, restart_loop, oom, network_error, dependency_failed
    severity TEXT DEFAULT 'medium',          -- low, medium, high, critical
    title TEXT NOT NULL,
    description TEXT,
    metadata_json TEXT,                      -- Additional context
    detected_at INTEGER NOT NULL,
    resolved_at INTEGER,                     -- NULL if unresolved
    resolution_action TEXT,                  -- What fixed it
    auto_resolve_attempted BOOLEAN DEFAULT 0,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_docker_issues_server_id ON docker_issues(server_id);
CREATE INDEX IF NOT EXISTS idx_docker_issues_container_id ON docker_issues(container_id);
CREATE INDEX IF NOT EXISTS idx_docker_issues_severity ON docker_issues(severity);
CREATE INDEX IF NOT EXISTS idx_docker_issues_resolved ON docker_issues(resolved_at);
CREATE INDEX IF NOT EXISTS idx_docker_issues_detected_at ON docker_issues(detected_at DESC);

-- Orchestration actions: log of actions taken by nazg
CREATE TABLE IF NOT EXISTS docker_orchestration_actions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    server_id INTEGER NOT NULL,
    stack_id INTEGER,
    action_type TEXT NOT NULL,               -- restart, start, stop, recreate, fix_issue
    target TEXT NOT NULL,                    -- Container/service/stack name
    reason TEXT,                             -- Why was this action taken
    triggered_by TEXT DEFAULT 'manual',      -- manual, auto, rule, issue
    status TEXT DEFAULT 'pending',           -- pending, running, success, failed
    started_at INTEGER NOT NULL,
    completed_at INTEGER,
    exit_code INTEGER,
    output TEXT,
    error TEXT,
    FOREIGN KEY (server_id) REFERENCES servers(id) ON DELETE CASCADE,
    FOREIGN KEY (stack_id) REFERENCES docker_stacks(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_orch_actions_server_id ON docker_orchestration_actions(server_id);
CREATE INDEX IF NOT EXISTS idx_orch_actions_stack_id ON docker_orchestration_actions(stack_id);
CREATE INDEX IF NOT EXISTS idx_orch_actions_status ON docker_orchestration_actions(status);
CREATE INDEX IF NOT EXISTS idx_orch_actions_started_at ON docker_orchestration_actions(started_at DESC);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (11, strftime('%s', 'now'));

COMMIT;

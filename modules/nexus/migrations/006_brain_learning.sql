-- Brain Failure Learning & Auto-Recovery schema
-- Version: 6

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

-- Extend workspace_failures with brain learning capabilities
ALTER TABLE workspace_failures ADD COLUMN error_location TEXT;
ALTER TABLE workspace_failures ADD COLUMN command_executed TEXT;
ALTER TABLE workspace_failures ADD COLUMN exit_code INTEGER;
ALTER TABLE workspace_failures ADD COLUMN changed_env TEXT;          -- JSON map of env changes
ALTER TABLE workspace_failures ADD COLUMN changed_system TEXT;       -- JSON map of system changes
ALTER TABLE workspace_failures ADD COLUMN severity TEXT DEFAULT 'medium';
ALTER TABLE workspace_failures ADD COLUMN tags_json TEXT;            -- JSON array
ALTER TABLE workspace_failures ADD COLUMN resolved_at INTEGER;
ALTER TABLE workspace_failures ADD COLUMN resolution_snapshot_id INTEGER REFERENCES workspace_snapshots(id) ON DELETE SET NULL;
ALTER TABLE workspace_failures ADD COLUMN resolution_success INTEGER;

CREATE INDEX IF NOT EXISTS idx_workspace_failures_timestamp
    ON workspace_failures(timestamp);
CREATE INDEX IF NOT EXISTS idx_workspace_failures_type
    ON workspace_failures(failure_type);

-- Pattern definitions learned from failures
CREATE TABLE IF NOT EXISTS brain_failure_patterns (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,

    -- Pattern identification
    pattern_signature TEXT NOT NULL,
    pattern_name TEXT,
    failure_type TEXT NOT NULL,

    -- Pattern characteristics
    error_regex TEXT,
    trigger_conditions_json TEXT,    -- JSON: what changes trigger this

    -- Statistical data
    occurrence_count INTEGER DEFAULT 1,
    first_seen INTEGER NOT NULL,
    last_seen INTEGER NOT NULL,

    -- Related failures (JSON array of failure IDs)
    failure_ids_json TEXT,

    -- Resolution strategies (JSON array)
    resolution_strategies_json TEXT,

    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE,
    UNIQUE(project_id, pattern_signature)
);

CREATE INDEX IF NOT EXISTS idx_brain_patterns_signature
    ON brain_failure_patterns(pattern_signature);
CREATE INDEX IF NOT EXISTS idx_brain_patterns_project
    ON brain_failure_patterns(project_id);
CREATE INDEX IF NOT EXISTS idx_brain_patterns_type
    ON brain_failure_patterns(failure_type);

-- Recovery actions and their success rates
CREATE TABLE IF NOT EXISTS brain_recovery_actions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    pattern_id INTEGER,
    failure_id INTEGER,

    -- Action definition
    action_type TEXT NOT NULL,       -- "restore_files", "restore_snapshot",
                                     -- "modify_config", "downgrade_dep"
    action_params_json TEXT,         -- JSON: parameters for action
    action_description TEXT,

    -- Execution statistics
    attempted_count INTEGER DEFAULT 0,
    success_count INTEGER DEFAULT 0,
    failure_count INTEGER DEFAULT 0,

    -- Timing
    avg_execution_time_ms INTEGER,

    -- Metadata
    confidence_score REAL DEFAULT 0.0,     -- 0.0 to 1.0
    requires_user_confirmation INTEGER DEFAULT 1,
    created_at INTEGER NOT NULL,
    last_attempted INTEGER,

    FOREIGN KEY(pattern_id) REFERENCES brain_failure_patterns(id) ON DELETE CASCADE,
    FOREIGN KEY(failure_id) REFERENCES workspace_failures(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_brain_recovery_pattern
    ON brain_recovery_actions(pattern_id);
CREATE INDEX IF NOT EXISTS idx_brain_recovery_failure
    ON brain_recovery_actions(failure_id);
CREATE INDEX IF NOT EXISTS idx_brain_recovery_confidence
    ON brain_recovery_actions(confidence_score);

-- Recovery execution history
CREATE TABLE IF NOT EXISTS brain_recovery_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    project_id INTEGER NOT NULL,
    failure_id INTEGER NOT NULL,
    action_id INTEGER,

    -- Execution details
    started_at INTEGER NOT NULL,
    completed_at INTEGER,
    success INTEGER,
    execution_time_ms INTEGER,

    -- Results
    output_log TEXT,
    verification_passed INTEGER,

    -- What was done (JSON)
    restored_files_json TEXT,
    modified_configs_json TEXT,

    -- Metadata
    execution_mode TEXT,             -- "auto", "manual", "suggested"
    user_approved INTEGER DEFAULT 0,

    FOREIGN KEY(project_id) REFERENCES projects(id) ON DELETE CASCADE,
    FOREIGN KEY(failure_id) REFERENCES workspace_failures(id) ON DELETE CASCADE,
    FOREIGN KEY(action_id) REFERENCES brain_recovery_actions(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_brain_recovery_history_failure
    ON brain_recovery_history(failure_id);
CREATE INDEX IF NOT EXISTS idx_brain_recovery_history_timestamp
    ON brain_recovery_history(started_at);
CREATE INDEX IF NOT EXISTS idx_brain_recovery_history_project
    ON brain_recovery_history(project_id);

-- Knowledge graph: relationships between failures
CREATE TABLE IF NOT EXISTS brain_failure_relationships (
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    source_failure_id INTEGER NOT NULL,
    related_failure_id INTEGER NOT NULL,

    relationship_type TEXT NOT NULL,  -- "similar", "caused_by", "preceded_by"
    similarity_score REAL,            -- 0.0 to 1.0

    created_at INTEGER NOT NULL,

    FOREIGN KEY(source_failure_id) REFERENCES workspace_failures(id) ON DELETE CASCADE,
    FOREIGN KEY(related_failure_id) REFERENCES workspace_failures(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_brain_relationships_source
    ON brain_failure_relationships(source_failure_id);
CREATE INDEX IF NOT EXISTS idx_brain_relationships_related
    ON brain_failure_relationships(related_failure_id);
CREATE INDEX IF NOT EXISTS idx_brain_relationships_type
    ON brain_failure_relationships(relationship_type);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (6, strftime('%s', 'now'));

COMMIT;

-- Migration 004: Test runner tables
-- Add tables for tracking test executions, results, and coverage

-- Test runs (high-level test execution)
CREATE TABLE IF NOT EXISTS test_runs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  project_id INTEGER NOT NULL,
  framework TEXT NOT NULL,        -- 'gtest', 'pytest', 'cargo', etc.
  timestamp INTEGER NOT NULL,     -- Unix timestamp
  duration_ms INTEGER NOT NULL,
  exit_code INTEGER NOT NULL,
  total_tests INTEGER NOT NULL,
  passed INTEGER NOT NULL,
  failed INTEGER NOT NULL,
  skipped INTEGER NOT NULL,
  errors INTEGER NOT NULL,
  triggered_by TEXT,              -- 'manual', 'auto', 'pre-commit'
  FOREIGN KEY (project_id) REFERENCES projects(id)
);

CREATE INDEX IF NOT EXISTS idx_test_runs_project ON test_runs(project_id);
CREATE INDEX IF NOT EXISTS idx_test_runs_timestamp ON test_runs(timestamp);

-- Individual test cases
CREATE TABLE IF NOT EXISTS test_results (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL,
  suite TEXT,                     -- Test suite/class name
  name TEXT NOT NULL,             -- Test name
  status TEXT NOT NULL,           -- 'passed', 'failed', 'skipped', 'error', 'timeout'
  duration_ms INTEGER,
  message TEXT,                   -- Failure/skip message
  file TEXT,                      -- Source file
  line INTEGER,                   -- Line number
  FOREIGN KEY (run_id) REFERENCES test_runs(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_test_results_run ON test_results(run_id);
CREATE INDEX IF NOT EXISTS idx_test_results_status ON test_results(status);
CREATE INDEX IF NOT EXISTS idx_test_results_name ON test_results(name);

-- Test coverage tracking
CREATE TABLE IF NOT EXISTS test_coverage (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  run_id INTEGER NOT NULL,
  file_path TEXT NOT NULL,
  line_coverage REAL,             -- 0.0 - 1.0
  branch_coverage REAL,           -- 0.0 - 1.0
  lines_covered INTEGER,
  lines_total INTEGER,
  branches_covered INTEGER,
  branches_total INTEGER,
  FOREIGN KEY (run_id) REFERENCES test_runs(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_coverage_run ON test_coverage(run_id);
CREATE INDEX IF NOT EXISTS idx_coverage_file ON test_coverage(file_path);

-- Record this migration
INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (4, strftime('%s', 'now'));

-- Migration 012: Add metadata table for encryption salt
CREATE TABLE IF NOT EXISTS nazg_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (12, strftime('%s', 'now'));

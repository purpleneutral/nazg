-- Agent container metadata
-- Version: 10

PRAGMA foreign_keys = ON;

BEGIN TRANSACTION;

ALTER TABLE servers ADD COLUMN agent_container_strategy TEXT DEFAULT 'binary';
ALTER TABLE servers ADD COLUMN agent_container_local_tar TEXT;
ALTER TABLE servers ADD COLUMN agent_container_remote_tar TEXT;
ALTER TABLE servers ADD COLUMN agent_container_image TEXT;

INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (10, strftime('%s', 'now'));

COMMIT;

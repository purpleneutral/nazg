-- Git server admin token storage
PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;

ALTER TABLE git_servers ADD COLUMN admin_token TEXT;

INSERT OR IGNORE INTO schema_version (version, applied_at)
VALUES (7, strftime('%s', 'now'));

COMMIT;
PRAGMA foreign_keys=ON;

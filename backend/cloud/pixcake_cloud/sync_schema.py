SYNC_SCHEMA = """
CREATE TABLE uploads (
 id TEXT PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id),
 operation_id TEXT NOT NULL, request_hash TEXT NOT NULL, space_id TEXT NOT NULL REFERENCES spaces(id),
 project_id TEXT NOT NULL, base_revision INTEGER NOT NULL, manifest TEXT NOT NULL,
 size_bytes INTEGER NOT NULL, expires_at INTEGER NOT NULL,
 committed_revision INTEGER NOT NULL DEFAULT 0,
 UNIQUE(account_id, operation_id)
);
CREATE INDEX uploads_by_account ON uploads(account_id,committed_revision,expires_at);
CREATE TABLE project_versions (
 project_id TEXT NOT NULL REFERENCES projects(id), revision INTEGER NOT NULL,
 upload_id TEXT NOT NULL REFERENCES uploads(id), PRIMARY KEY(project_id,revision)
);
"""

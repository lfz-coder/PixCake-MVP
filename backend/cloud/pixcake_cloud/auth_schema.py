AUTH_SCHEMA = """
CREATE TABLE credentials (
 phone TEXT PRIMARY KEY, account_id TEXT NOT NULL UNIQUE REFERENCES accounts(id), password_hash TEXT NOT NULL
);
CREATE TABLE sms_challenges (
 id TEXT PRIMARY KEY, phone TEXT NOT NULL, purpose TEXT NOT NULL,
 code_hash TEXT NOT NULL, expires_at INTEGER NOT NULL, sent_at INTEGER NOT NULL,
 attempts INTEGER NOT NULL DEFAULT 0, delivered INTEGER NOT NULL DEFAULT 0,
 UNIQUE(phone,purpose)
);
CREATE TABLE auth_limits (
 bucket TEXT PRIMARY KEY, count INTEGER NOT NULL, reset_at INTEGER NOT NULL
);
ALTER TABLE sessions ADD COLUMN family_id TEXT;
CREATE INDEX sessions_by_family ON sessions(family_id);
CREATE TABLE refresh_sessions (
 token_hash TEXT PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id),
 family_id TEXT NOT NULL, expires_at INTEGER NOT NULL, used INTEGER NOT NULL DEFAULT 0,
 revoked INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX refresh_by_family ON refresh_sessions(family_id);
CREATE INDEX refresh_by_account ON refresh_sessions(account_id);
"""

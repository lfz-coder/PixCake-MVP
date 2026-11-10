import hashlib
import os
import re
import secrets
import sqlite3
import time
from contextlib import closing, contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from .auth_schema import AUTH_SCHEMA
from .sync_schema import SYNC_SCHEMA

SCHEMA_VERSION = 3
APPLICATION_ID = 0x50434344  # PCCD: distinguish this DB from desktop project DBs.
PERMISSIONS = ("read", "write", "manage")
SCHEMA = """
CREATE TABLE accounts (
 id TEXT PRIMARY KEY, name TEXT NOT NULL,
 enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),
 directory_revision INTEGER NOT NULL DEFAULT 0 CHECK(directory_revision >= 0)
);
CREATE TABLE spaces (
 id TEXT PRIMARY KEY, name TEXT NOT NULL CHECK(length(CAST(name AS BLOB)) BETWEEN 1 AND 1024),
 kind TEXT NOT NULL CHECK(kind IN ('personal','team')),
 owner_id TEXT NOT NULL REFERENCES accounts(id),
 projects_revision INTEGER NOT NULL DEFAULT 0 CHECK(projects_revision >= 0)
);
CREATE UNIQUE INDEX one_personal_space ON spaces(owner_id) WHERE kind='personal';
CREATE TABLE memberships (
 account_id TEXT NOT NULL REFERENCES accounts(id), space_id TEXT NOT NULL REFERENCES spaces(id),
 can_read INTEGER NOT NULL CHECK(can_read IN (0,1)),
 can_write INTEGER NOT NULL CHECK(can_write IN (0,1)),
 can_manage INTEGER NOT NULL CHECK(can_manage IN (0,1)),
 PRIMARY KEY(account_id,space_id)
);
CREATE TABLE projects (
 id TEXT PRIMARY KEY, space_id TEXT NOT NULL REFERENCES spaces(id),
 name TEXT NOT NULL CHECK(length(CAST(name AS BLOB)) BETWEEN 1 AND 1024),
 revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0),
 updated_at INTEGER NOT NULL CHECK(updated_at >= 0),
 size_bytes INTEGER NOT NULL DEFAULT 0 CHECK(size_bytes >= 0)
);
CREATE INDEX projects_by_space ON projects(space_id,id);
CREATE TABLE sessions (
 token_hash TEXT PRIMARY KEY, account_id TEXT NOT NULL REFERENCES accounts(id),
 expires_at INTEGER NOT NULL CHECK(expires_at >= 0),
 revoked INTEGER NOT NULL DEFAULT 0 CHECK(revoked IN (0,1))
);
CREATE INDEX sessions_by_account ON sessions(account_id);
"""


def validate_name(name: str):
    if not isinstance(name, str) or not name or len(name.encode("utf-8")) > 1024:
        raise ValueError("Name must contain 1–1024 UTF-8 bytes.")
    if any(ord(c) < 32 or ord(c) == 127 for c in name):
        raise ValueError("Name contains control characters.")


def new_id(prefix: str) -> str:
    return prefix + "_" + secrets.token_hex(16)


@dataclass(frozen=True)
class Principal:
    account_id: str
    token_hash: str = field(repr=False)
    expires_at: int
    directory_revision: int


class Store:
    def __init__(self, database: Path, clock: Callable = time.time):
        self.database = Path(database).resolve()
        self.clock = clock

    def initialize(self):
        self.database.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        try:
            fd = os.open(self.database, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        except FileExistsError:
            pass
        else:
            os.close(fd)
        with closing(sqlite3.connect(self.database, timeout=2)) as connection:
            version = connection.execute("PRAGMA user_version").fetchone()[0]
            application = connection.execute("PRAGMA application_id").fetchone()[0]
            if version == SCHEMA_VERSION and application == APPLICATION_ID:
                return
            if version in (1, 2) and application == APPLICATION_ID:
                connection.execute("PRAGMA foreign_keys=ON")
                with connection:
                    connection.executescript("BEGIN IMMEDIATE;" + (AUTH_SCHEMA if version == 1 else "") + SYNC_SCHEMA +
                                             f"PRAGMA user_version={SCHEMA_VERSION}; COMMIT;")
                return
            if (version != 0 or application != 0
                    or connection.execute("SELECT 1 FROM sqlite_master WHERE type='table'").fetchone()):
                raise RuntimeError("Unsupported cloud database schema.")
            connection.execute("PRAGMA foreign_keys=ON")
            connection.execute("PRAGMA journal_mode=WAL")
            with connection:
                connection.executescript("BEGIN IMMEDIATE;" + SCHEMA + AUTH_SCHEMA + SYNC_SCHEMA +
                    f"PRAGMA application_id={APPLICATION_ID}; PRAGMA user_version={SCHEMA_VERSION}; COMMIT;")

    @contextmanager
    def transaction(self, *, write=False):
        # mode=rw prevents a missing/mistyped path from silently creating a DB.
        connection = sqlite3.connect(self.database.as_uri() + "?mode=rw", uri=True, timeout=2)
        connection.row_factory = sqlite3.Row
        try:
            connection.execute("PRAGMA foreign_keys=ON")
            connection.execute("BEGIN IMMEDIATE" if write else "BEGIN")
            if (connection.execute("PRAGMA user_version").fetchone()[0] != SCHEMA_VERSION
                    or connection.execute("PRAGMA application_id").fetchone()[0] != APPLICATION_ID):
                raise RuntimeError("Unsupported cloud database schema.")
            yield connection
            connection.commit()
        except BaseException:
            connection.rollback()
            raise
        finally:
            connection.close()

    def check_ready(self):
        with self.transaction() as connection:
            connection.execute("SELECT id FROM accounts LIMIT 1").fetchone()

    def _create_space(self, connection, owner_id, name, kind):
        space_id = new_id("sp")
        connection.execute("INSERT INTO spaces(id,name,kind,owner_id) VALUES(?,?,?,?)",
                           (space_id, name, kind, owner_id))
        # Explicit initial grant by the trusted provisioning operation.
        connection.execute("INSERT INTO memberships VALUES(?,?,1,1,1)", (owner_id, space_id))
        connection.execute("UPDATE accounts SET directory_revision=directory_revision+1 WHERE id=?", (owner_id,))
        return space_id

    def create_account(self, name: str) -> dict:
        validate_name(name)
        with self.transaction(write=True) as connection:
            account_id = new_id("acc")
            connection.execute("INSERT INTO accounts(id,name) VALUES(?,?)", (account_id, name))
            personal = self._create_space(connection, account_id, "个人空间", "personal")
        return {"account_id": account_id, "personal_space_id": personal}

    def create_team(self, owner_id: str, name: str) -> str:
        validate_name(name)
        with self.transaction(write=True) as connection:
            return self._create_space(connection, owner_id, name, "team")

    def set_permissions(self, account_id: str, space_id: str, permissions: list[str]):
        if len(permissions) != len(set(permissions)) or any(p not in PERMISSIONS for p in permissions):
            raise ValueError("Invalid explicit permissions.")
        with self.transaction(write=True) as connection:
            space = connection.execute("SELECT kind,owner_id FROM spaces WHERE id=?", (space_id,)).fetchone()
            if space is None or (space["kind"] == "personal" and space["owner_id"] != account_id):
                raise ValueError("Membership cannot be assigned to this space.")
            connection.execute("""INSERT INTO memberships VALUES(?,?,?,?,?)
                ON CONFLICT(account_id,space_id) DO UPDATE SET can_read=excluded.can_read,
                can_write=excluded.can_write,can_manage=excluded.can_manage""",
                (account_id, space_id, *(int(p in permissions) for p in PERMISSIONS)))
            connection.execute("UPDATE accounts SET directory_revision=directory_revision+1 WHERE id=?", (account_id,))

    def revoke_membership(self, account_id: str, space_id: str):
        with self.transaction(write=True) as connection:
            space = connection.execute("SELECT kind FROM spaces WHERE id=?", (space_id,)).fetchone()
            if space is None or space["kind"] == "personal":
                raise ValueError("Only team membership can be removed.")
            changed = connection.execute("DELETE FROM memberships WHERE account_id=? AND space_id=?",
                                         (account_id, space_id)).rowcount
            if changed:
                connection.execute("UPDATE accounts SET directory_revision=directory_revision+1 WHERE id=?", (account_id,))

    def create_project(self, space_id: str, name: str) -> str:
        validate_name(name)
        with self.transaction(write=True) as connection:
            project_id = new_id("prj")
            connection.execute("INSERT INTO projects(id,space_id,name,updated_at) VALUES(?,?,?,?)",
                               (project_id, space_id, name, int(self.clock())))
            connection.execute("UPDATE spaces SET projects_revision=projects_revision+1 WHERE id=?", (space_id,))
        return project_id

    def issue_session(self, account_id: str, *, ttl_seconds=3600, deliver: Callable | None = None) -> str:
        if type(ttl_seconds) is not int or not 1 <= ttl_seconds <= 86400:
            raise ValueError("Session TTL must be between 1 and 86400 seconds.")
        with self.transaction(write=True) as connection:
            row = connection.execute("SELECT enabled FROM accounts WHERE id=?", (account_id,)).fetchone()
            if row is None or not row["enabled"]:
                raise ValueError("An enabled account is required.")
            token = secrets.token_urlsafe(32)
            digest = hashlib.sha256(token.encode("ascii")).hexdigest()
            connection.execute("INSERT INTO sessions(token_hash,account_id,expires_at) VALUES(?,?,?)",
                               (digest, account_id, int(self.clock()) + ttl_seconds))
            # Local provisioning can durably deliver a token before committing
            # its session. Failed delivery rolls back the session transaction.
            if deliver is not None:
                deliver(token)
        return token

    def set_account_enabled(self, account_id: str, enabled: bool):
        with self.transaction(write=True) as connection:
            connection.execute("UPDATE accounts SET enabled=? WHERE id=?", (int(enabled), account_id))
            if not enabled:
                connection.execute("UPDATE sessions SET revoked=1 WHERE account_id=?", (account_id,))
                connection.execute("UPDATE refresh_sessions SET revoked=1 WHERE account_id=?", (account_id,))

    def authenticate(self, connection, token: str) -> Principal | None:
        if not re.fullmatch(r"[A-Za-z0-9_-]{43}", token):
            return None
        digest = hashlib.sha256(token.encode("ascii")).hexdigest()
        row = connection.execute("""SELECT a.id,a.directory_revision,s.expires_at FROM sessions s
            JOIN accounts a ON a.id=s.account_id
            WHERE s.token_hash=? AND s.revoked=0 AND s.expires_at>? AND a.enabled=1""",
            (digest, int(self.clock()))).fetchone()
        return None if row is None else Principal(row["id"], digest, row["expires_at"], row["directory_revision"])

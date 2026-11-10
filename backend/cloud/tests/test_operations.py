import json
import os
import sqlite3
import stat
import subprocess
import sys
from pathlib import Path

import pytest

from pixcake_cloud.store import Store


ROOT = Path(__file__).parents[1]


def command(database, *arguments):
    return subprocess.run([sys.executable, "-m", "pixcake_cloud", "--db", str(database), *arguments],
                          cwd=ROOT, capture_output=True, text=True, timeout=10)


def test_cli_initialization_session_delivery_and_no_overwrite(tmp_path):
    database = tmp_path / "cloud.sqlite"
    assert command(database, "init").returncode == 0
    assert command(database, "init").returncode == 0
    account = json.loads(command(database, "create-account", "--name", "本地测试账号").stdout)["account_id"]
    destination = tmp_path / "account.token"
    result = command(database, "issue-session", "--account", account, "--output", str(destination))
    assert result.returncode == 0
    token = destination.read_text().strip()
    assert len(token) == 43
    assert token not in result.stdout + result.stderr
    if os.name == "posix":
        assert stat.S_IMODE(destination.stat().st_mode) == 0o600
        assert stat.S_IMODE(database.stat().st_mode) == 0o600
    again = command(database, "issue-session", "--account", account, "--output", str(destination))
    assert again.returncode == 1 and destination.read_text().strip() == token
    bad = tmp_path / "invalid.token"
    assert command(database, "issue-session", "--account", "missing", "--output", str(bad)).returncode == 1
    assert not bad.exists()


def test_failed_delivery_and_foreign_key_write_roll_back(tmp_path):
    store = Store(tmp_path / "cloud.sqlite")
    store.initialize()
    account = store.create_account("Rollback")
    def cannot_write(_token):
        raise OSError("simulated disk full")
    with pytest.raises(OSError):
        store.issue_session(account["account_id"], deliver=cannot_write)
    with pytest.raises(sqlite3.IntegrityError):
        store.create_team("missing", "Invalid owner")
    with store.transaction() as connection:
        assert connection.execute("SELECT count(*) FROM sessions").fetchone()[0] == 0
        assert connection.execute("SELECT count(*) FROM spaces").fetchone()[0] == 1


def test_revocation_after_read_snapshot_applies_to_next_transaction(tmp_path):
    store = Store(tmp_path / "cloud.sqlite")
    store.initialize()
    alice = store.create_account("Alice")["account_id"]
    bob = store.create_account("Bob")["account_id"]
    team = store.create_team(alice, "Team")
    store.set_permissions(bob, team, ["read"])
    token = store.issue_session(bob)
    with store.transaction() as reading:
        assert store.authenticate(reading, token) is not None  # Establish the read snapshot.
        store.revoke_membership(bob, team)
        assert reading.execute("SELECT 1 FROM memberships WHERE account_id=? AND space_id=?", (bob, team)).fetchone()
    with store.transaction() as reading:
        assert not reading.execute("SELECT 1 FROM memberships WHERE account_id=? AND space_id=?", (bob, team)).fetchone()


def test_foreign_database_with_same_version_is_never_adopted(tmp_path):
    path = tmp_path / "desktop.sqlite"
    with sqlite3.connect(path) as connection:
        connection.execute("PRAGMA user_version=1")
        connection.execute("CREATE TABLE projects(id TEXT PRIMARY KEY)")
        connection.execute("INSERT INTO projects VALUES('local-project')")
    store = Store(path)
    with pytest.raises(RuntimeError, match="schema"):
        store.initialize()
    with pytest.raises(RuntimeError, match="schema"):
        store.create_account("Cannot use desktop DB")
    with sqlite3.connect(path) as connection:
        assert connection.execute("SELECT id FROM projects").fetchall() == [("local-project",)]
        assert connection.execute("PRAGMA application_id").fetchone()[0] == 0


def test_backend_does_not_enter_desktop_build_or_import_desktop_modules():
    repo = ROOT.parents[1]
    for path in (repo / "CMakeLists.txt", repo / "cmake/PixcakeBundle.cmake"):
        assert "backend/cloud" not in path.read_text()
    for path in (ROOT / "pixcake_cloud").glob("*.py"):
        assert "sys.path" not in path.read_text()
        assert "ctypes" not in path.read_text()

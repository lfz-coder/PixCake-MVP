import hashlib
import json
import sqlite3
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from pixcake_cloud.api import create_app
from pixcake_cloud.config import Settings
from pixcake_cloud.store import Store


class Clock:
    value = 2_000_000_000

    def __call__(self):
        return self.value


@pytest.fixture
def setup(tmp_path):
    clock = Clock()
    settings = Settings(tmp_path / "cloud.sqlite", "test-only-signing-secret-32-bytes-long")
    store = Store(settings.database, clock=clock)
    store.initialize()
    alice = store.create_account("Alice")
    bob = store.create_account("Bob")
    token = store.issue_session(alice["account_id"])
    other = store.issue_session(bob["account_id"])
    client = TestClient(create_app(settings, clock=clock))
    yield store, clock, settings, client, alice, bob, token, other
    client.close()


def auth(token):
    return {"Authorization": f"Bearer {token}"}


def test_bcl01_identity_session_hash_and_logout_persist(setup):
    store, clock, settings, client, alice, _, token, _ = setup
    response = client.get("/v1/account", headers=auth(token))
    assert response.status_code == 200
    assert response.json() == {"api_version": 1, "account_id": alice["account_id"],
                               "session_expires_at": clock.value + 3600}
    with sqlite3.connect(settings.database) as connection:
        dump = "\n".join(connection.iterdump())
    assert token not in dump
    assert hashlib.sha256(token.encode()).hexdigest() in dump
    assert client.delete("/v1/session", headers=auth(token)).status_code == 204
    with TestClient(create_app(settings, clock=clock)) as restarted:
        assert restarted.get("/v1/account", headers=auth(token)).status_code == 401


@pytest.mark.parametrize("headers", [{}, {"Authorization": "Basic abc"},
    {"Authorization": "Bearer not-a-valid-token"}, {"Authorization": "Bearer "}])
def test_bcl01_missing_and_malformed_authentication(setup, headers):
    client = setup[3]
    response = client.get("/v1/spaces", headers=headers)
    assert response.status_code == 401
    assert response.headers["www-authenticate"] == "Bearer"
    assert response.json()["error"]["code"] == "unauthorized"


def test_bcl01_expiry_and_disabled_account(setup):
    store, clock, _, client, alice, _, token, _ = setup
    short = store.issue_session(alice["account_id"], ttl_seconds=1)
    clock.value += 1
    assert client.get("/v1/spaces", headers=auth(short)).status_code == 401
    assert client.get("/v1/spaces", headers=auth(token)).status_code == 200
    store.set_account_enabled(alice["account_id"], False)
    assert client.get("/v1/account", headers=auth(token)).status_code == 401
    with pytest.raises(ValueError):
        store.issue_session(alice["account_id"])
    for ttl in (0, -1, 86401):
        with pytest.raises(ValueError):
            store.issue_session(alice["account_id"], ttl_seconds=ttl)


def test_bcl02_account_isolation_and_independent_permissions(setup):
    store, _, _, client, alice, bob, token, other = setup
    team = store.create_team(alice["account_id"], "团队")
    store.create_project(team, "团队项目")
    store.set_permissions(bob["account_id"], team, ["manage"])
    mine = client.get("/v1/spaces", headers=auth(other)).json()["items"]
    assert {x["id"] for x in mine} == {bob["personal_space_id"], team}
    assert next(x for x in mine if x["id"] == team)["permissions"] == ["manage"]
    assert client.get(f"/v1/spaces/{team}/projects", headers=auth(other)).status_code == 403
    store.set_permissions(bob["account_id"], team, ["write"])
    assert client.get(f"/v1/spaces/{team}/projects", headers=auth(other)).status_code == 403
    store.set_permissions(bob["account_id"], team, ["read"])
    assert client.get(f"/v1/spaces/{team}/projects", headers=auth(other)).status_code == 200
    assert client.get(f"/v1/spaces/{bob['personal_space_id']}/projects", headers=auth(token)).status_code == 403
    assert client.get("/v1/spaces/sp_missing/projects", headers=auth(token)).status_code == 403
    with pytest.raises(ValueError):
        store.set_permissions(bob["account_id"], alice["personal_space_id"], ["read"])


def test_bcl03_space_and_project_pagination(setup):
    store, _, _, client, alice, _, token, _ = setup
    team = store.create_team(alice["account_id"], "分页团队")
    expected = {store.create_project(team, f"照片 {i}") for i in range(5)}
    response = client.get("/v1/spaces", params={"limit": 1}, headers=auth(token))
    first = response.json()
    assert len(first["items"]) == 1 and first["next_cursor"]
    second = client.get("/v1/spaces", params={"limit": 1, "cursor": first["next_cursor"]}, headers=auth(token)).json()
    assert {first["items"][0]["id"], second["items"][0]["id"]} == {alice["personal_space_id"], team}
    assert second["next_cursor"] == ""
    seen = []
    cursor = ""
    for _ in range(3):
        reply = client.get(f"/v1/spaces/{team}/projects", params={"limit": 2, "cursor": cursor}, headers=auth(token))
        assert reply.status_code == 200
        page = reply.json()
        assert page["api_version"] == 1
        seen += [x["id"] for x in page["items"]]
        assert all(x["space_id"] == team and x["revision"] == 0 and x["size_bytes"] == 0 for x in page["items"])
        cursor = page["next_cursor"]
    assert seen == sorted(expected) and cursor == ""


def test_bcl03_cursor_binding_tampering_and_expiry(setup):
    store, clock, _, client, alice, _, token, other = setup
    team = store.create_team(alice["account_id"], "分页")
    store.create_team(alice["account_id"], "其他")
    cursor = client.get("/v1/spaces", params={"limit": 1}, headers=auth(token)).json()["next_cursor"]
    for who, route, limit, value in (
        (other, "/v1/spaces", 1, cursor),
        (store.issue_session(alice["account_id"]), "/v1/spaces", 1, cursor),
        (token, f"/v1/spaces/{team}/projects", 1, cursor),
        (token, "/v1/spaces", 2, cursor),
        (token, "/v1/spaces", 1, "bad." + cursor),
    ):
        reply = client.get(route, params={"limit": limit, "cursor": value}, headers=auth(who))
        assert reply.status_code == 400
        assert reply.json()["error"]["code"] == "invalid_cursor"
    clock.value += 299
    page2 = client.get("/v1/spaces", params={"limit": 1, "cursor": cursor}, headers=auth(token)).json()
    assert page2["next_cursor"]
    clock.value += 1
    expired = client.get("/v1/spaces", params={"limit": 1, "cursor": page2["next_cursor"]}, headers=auth(token))
    assert expired.status_code == 410  # Later pages never extend the first-page TTL.


def test_bcl04_directory_changes_and_permission_revocation(setup):
    store, _, _, client, alice, bob, token, other = setup
    team = store.create_team(alice["account_id"], "目录修订")
    store.set_permissions(bob["account_id"], team, ["read"])
    for name in ("A", "B"):
        store.create_project(team, name)
    route = f"/v1/spaces/{team}/projects"
    cursor = client.get(route, params={"limit": 1}, headers=auth(other)).json()["next_cursor"]
    store.create_project(team, "C")
    changed = client.get(route, params={"limit": 1, "cursor": cursor}, headers=auth(other))
    assert changed.status_code == 409 and changed.json()["error"]["code"] == "directory_changed"
    cursor = client.get(route, params={"limit": 1}, headers=auth(other)).json()["next_cursor"]
    store.revoke_membership(bob["account_id"], team)
    assert client.get(route, params={"limit": 1, "cursor": cursor}, headers=auth(other)).status_code == 403
    spaces_cursor = client.get("/v1/spaces", params={"limit": 1}, headers=auth(token)).json()["next_cursor"]
    store.create_team(alice["account_id"], "新空间")
    assert client.get("/v1/spaces", params={"limit": 1, "cursor": spaces_cursor}, headers=auth(token)).status_code == 409


@pytest.mark.parametrize("query", ["limit=0", "limit=101", "limit=-1", "limit=x", "limit=1&limit=2",
    "account_id=other", "permissions=manage", "cursor=中文", "cursor=" + "x" * 4097])
def test_bcl05_invalid_query_is_bounded_and_redacted(setup, query):
    client, token = setup[3], setup[6]
    response = client.get("/v1/spaces?" + query, headers=auth(token))
    assert response.status_code == 400
    assert response.json()["error"]["code"] == "invalid_request"
    assert query not in response.text


def test_bcl05_request_envelope_limits(setup):
    client, token = setup[3], setup[6]
    assert client.get("/v1/spaces?cursor=" + "x" * 8200, headers=auth(token)).status_code == 413
    assert client.get("/v1/spaces", headers={**auth(token), "X-Large": "x" * 32768}).status_code == 431
    assert client.get("/v1/spaces", headers=[*auth(token).items(), *[(f"X-{i}", "") for i in range(129)]]).status_code == 431
    assert client.get("/v1/spaces", headers=[*auth(token).items(), *auth(token).items()]).status_code == 400
    assert client.request("GET", "/v1/spaces", headers=auth(token), content=b"unwanted").status_code == 400
    assert client.post("/v1/spaces", headers=auth(token)).status_code == 405
    assert client.get("/docs").status_code == 404
    assert client.get("/__fixture__/body").status_code == 404
    assert client.post("/__fixture__/mode/offline").status_code == 404


def test_bcl05_storage_validates_names_and_permissions(setup):
    store, _, _, _, alice, _, _, _ = setup
    for name in ("", "bad\nname", "x" * 1025, "中" * 342, "\ud800"):
        with pytest.raises(ValueError):
            store.create_team(alice["account_id"], name)
    assert store.create_team(alice["account_id"], "中" * 341)
    for capabilities in (["owner"], ["read", "read"]):
        with pytest.raises(ValueError):
            store.set_permissions(alice["account_id"], alice["personal_space_id"], capabilities)


def test_bcl06_errors_do_not_echo_inputs_or_log_secrets(setup, caplog, monkeypatch):
    _, _, _, client, _, _, token, _ = setup
    for response in (client.get("/v1/account", headers=auth("private-marker")),
                     client.get("/v1/spaces?private-marker=secret", headers=auth(token)),
                     client.get("/not-found")):
        assert response.headers["cache-control"] == "no-store"
        assert "private-marker" not in response.text and token not in response.text
    def fail(*args, **kwargs):
        raise sqlite3.OperationalError("private-marker /private/database.sqlite " + token)
    monkeypatch.setattr(Store, "authenticate", fail)
    response = client.get("/v1/account", headers=auth(token))
    assert response.status_code == 503
    assert response.json()["error"]["code"] == "storage_unavailable"
    assert "private-marker" not in response.text + caplog.text
    assert token not in response.text + caplog.text


def test_bcl07_restart_empty_directory_and_schema_guard(setup, tmp_path):
    store, clock, settings, client, alice, _, token, _ = setup
    team = store.create_team(alice["account_id"], "持久空间")
    project = store.create_project(team, "持久项目")
    with TestClient(create_app(settings, clock=clock)) as restarted:
        reply = restarted.get(f"/v1/spaces/{team}/projects", headers=auth(token))
        assert reply.json()["items"][0]["id"] == project
    empty = client.get(f"/v1/spaces/{alice['personal_space_id']}/projects", headers=auth(token))
    assert empty.json() == {"api_version": 1, "items": [], "next_cursor": ""}
    newer = tmp_path / "newer.sqlite"
    with sqlite3.connect(newer) as connection:
        connection.execute("PRAGMA user_version=99")
    with pytest.raises(RuntimeError, match="schema"):
        Store(newer).initialize()
    with sqlite3.connect(newer) as connection:
        assert connection.execute("PRAGMA user_version").fetchone()[0] == 99
    with pytest.raises((RuntimeError, sqlite3.Error)):
        create_app(Settings(tmp_path / "missing.sqlite", settings.cursor_secret))
    assert not (tmp_path / "missing.sqlite").exists()


def test_settings_require_explicit_secret_and_schema_matches(setup):
    with pytest.raises(ValueError):
        Settings(setup[2].database, "weak")
    expected = json.loads((Path(__file__).parents[1] / "openapi.json").read_text())
    assert create_app(setup[2]).openapi() == expected

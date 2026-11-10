import json
import sqlite3
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from pixcake_cloud.api import create_app
from pixcake_cloud.config import Settings
from pixcake_cloud.sms import DevelopmentSmsSender, SdkSmsSender
from pixcake_cloud.store import APPLICATION_ID, SCHEMA, SCHEMA_VERSION, Store

PHONE = "13800000001"
PASSWORD = "Correct horse battery! cloud"
NEW_PASSWORD = "Another secure passphrase 2026"


@pytest.fixture
def environment(tmp_path):
    clock = [2_000_000_000]
    settings = Settings(tmp_path / "auth.sqlite", "test-auth-root-secret-only-32-bytes", True, "mock")
    store = Store(settings.database, clock=lambda: clock[0])
    store.initialize()
    with TestClient(create_app(settings, clock=store.clock)) as client:
        yield store, clock, settings, client


def challenge(client, purpose="register", phone=PHONE):
    response = client.post("/v1/auth/code", json={"phone": phone, "purpose": purpose})
    assert response.status_code == 200
    value = response.json()
    assert len(value["development_code"]) == 6 and value["development_code"].isdigit()
    return {"phone": phone, "challenge_id": value["challenge_id"], "code": value["development_code"]}


def register(client):
    data = challenge(client)
    reply = client.post("/v1/auth/register", json={**data, "password": PASSWORD})
    assert reply.status_code == 200
    return reply.json(), data


def access(client, tokens):
    return client.get("/v1/spaces", headers={"Authorization": "Bearer " + tokens["access_token"]})


def test_registration_password_sms_login_and_no_plaintext(environment):
    store, _, _, client = environment
    tokens, code = register(client)
    spaces = access(client, tokens).json()["items"]
    assert len(spaces) == 1 and spaces[0]["kind"] == "personal"
    assert client.post("/v1/auth/register", json={**code, "password": PASSWORD}).status_code == 400
    password = client.post("/v1/auth/password/login", json={"phone": PHONE, "password": PASSWORD})
    assert password.status_code == 200 and password.json()["account_id"] == tokens["account_id"]
    sms = client.post("/v1/auth/sms/login", json=challenge(client, "login"))
    assert sms.status_code == 200 and sms.json()["account_id"] == tokens["account_id"]
    with store.transaction() as db:
        hashed = db.execute("SELECT password_hash FROM credentials").fetchone()[0]
        assert hashed.startswith("$argon2id$v=19$m=65536,t=3,p=4$")
        dump = "\n".join(db.iterdump())
    for secret in (PASSWORD, tokens["access_token"], tokens["refresh_token"]):
        assert secret not in dump
    assert client.post("/v1/auth/password/login", json={"phone": PHONE, "password": NEW_PASSWORD}).json() == \
        client.post("/v1/auth/password/login", json={"phone": "13900000002", "password": PASSWORD}).json()


def test_otp_expiry_attempts_purpose_phone_and_resend(environment):
    _, clock, _, client = environment
    data = challenge(client)
    wrong = "000001" if data["code"] == "000000" else "000000"
    for _ in range(5):
        assert client.post("/v1/auth/register", json={**data, "code": wrong, "password": PASSWORD}).status_code == 400
    assert client.post("/v1/auth/register", json={**data, "password": PASSWORD}).status_code == 400
    assert client.post("/v1/auth/code", json={"phone": PHONE, "purpose": "register"}).status_code == 429
    clock[0] += 61
    next_code = challenge(client)
    assert next_code["challenge_id"] != data["challenge_id"]
    assert client.post("/v1/auth/sms/login", json=next_code).status_code == 400
    clock[0] += 300
    assert client.post("/v1/auth/register", json={**next_code, "password": PASSWORD}).json()["error"]["code"] == "code_expired"


def test_otp_cannot_cross_phone_or_be_consumed_twice(environment):
    _, _, _, client = environment
    data = challenge(client)
    assert client.post("/v1/auth/register", json={**data, "phone": "13900000002", "password": PASSWORD}).status_code == 400
    def attempt():
        return client.post("/v1/auth/register", json={**data, "password": PASSWORD}).status_code
    with ThreadPoolExecutor(2) as workers:
        outcomes = list(workers.map(lambda _: attempt(), range(2)))
    assert sorted(outcomes) == [200, 400]


def test_refresh_rotation_reuse_and_logout_family(environment):
    _, _, _, client = environment
    first, _ = register(client)
    second = client.post("/v1/auth/refresh", json={"refresh_token": first["refresh_token"]}).json()
    assert access(client, first).status_code == 401 and access(client, second).status_code == 200
    assert first["refresh_token"] != second["refresh_token"]
    assert client.post("/v1/auth/refresh", json={"refresh_token": first["refresh_token"]}).status_code == 401
    assert access(client, second).status_code == 401
    fresh = client.post("/v1/auth/password/login", json={"phone": PHONE, "password": PASSWORD}).json()
    assert client.delete("/v1/session", headers={"Authorization": "Bearer " + fresh["access_token"]}).status_code == 204
    assert client.post("/v1/auth/refresh", json={"refresh_token": fresh["refresh_token"]}).status_code == 401


def test_password_reset_revokes_every_old_session(environment):
    _, _, _, client = environment
    old, _ = register(client)
    reset = client.post("/v1/auth/password/reset", json={**challenge(client, "reset"), "password": NEW_PASSWORD})
    assert reset.status_code == 200
    assert access(client, old).status_code == 401
    assert client.post("/v1/auth/refresh", json={"refresh_token": old["refresh_token"]}).status_code == 401
    assert client.post("/v1/auth/password/login", json={"phone": PHONE, "password": PASSWORD}).status_code == 401
    assert client.post("/v1/auth/password/login", json={"phone": PHONE, "password": NEW_PASSWORD}).status_code == 200


def test_logout_revokes_refresh_after_access_expiry(environment):
    _, clock, _, client = environment
    tokens, _ = register(client)
    clock[0] += 3601
    assert access(client, tokens).status_code == 401
    for _ in range(2):
        assert client.post("/v1/auth/logout", json={"refresh_token": tokens["refresh_token"]}).status_code == 204
    assert client.post("/v1/auth/refresh", json={"refresh_token": tokens["refresh_token"]}).status_code == 401


def test_hash_upgrade_and_resource_bound(environment):
    from argon2 import PasswordHasher
    from pixcake_cloud.passwords import PasswordBusy, Passwords
    store, _, _, client = environment
    register(client)
    old = PasswordHasher(memory_cost=8192, time_cost=1, parallelism=1).hash(PASSWORD)
    with store.transaction(write=True) as db:
        db.execute("UPDATE credentials SET password_hash=? WHERE phone=?", (old, PHONE))
    assert client.post("/v1/auth/password/login", json={"phone": PHONE, "password": PASSWORD}).status_code == 200
    with store.transaction() as db:
        assert "m=65536,t=3,p=4" in db.execute("SELECT password_hash FROM credentials").fetchone()[0]
    passwords = Passwords()
    assert passwords.verify(old.replace("m=8192", "m=999999999"), PASSWORD) == (False, None)
    with passwords.capacity(), passwords.capacity(), pytest.raises(PasswordBusy):
        passwords.hash(PASSWORD)


def test_password_policy_and_unicode(environment):
    _, _, _, client = environment
    data = challenge(client)
    for password in ("short", "passwordpassword", "a" * 129, "correct\x00horse battery staple"):
        assert client.post("/v1/auth/register", json={**data, "password": password}).json()["error"]["code"] == "password_weak"
    composed = "Un cafe\u0301 et une grande maison"
    assert client.post("/v1/auth/register", json={**data, "password": composed}).status_code == 200
    assert client.post("/v1/auth/password/login", json={"phone": PHONE, "password": "Un café et une grande maison"}).status_code == 200


def test_sdk_contract_and_production_never_returns_code(environment):
    store, _, settings, _ = environment
    delivered = []
    sender = SdkSmsSender(lambda *args: delivered.append(args))
    production = Settings(settings.database, settings.cursor_secret)
    with TestClient(create_app(production, clock=store.clock, sms_sender=sender)) as client:
        reply = client.post("/v1/auth/code", json={"phone": PHONE, "purpose": "register"}).json()
        assert reply["development_code"] == "" and len(delivered) == 1
        phone, code, purpose, identifier = delivered[0]
        assert phone == PHONE and purpose == "register" and identifier == reply["challenge_id"]
        assert client.post("/v1/auth/register", json={"phone": phone, "code": code,
            "challenge_id": identifier, "password": PASSWORD}).status_code == 200
    with pytest.raises(ValueError):
        Settings(settings.database, settings.cursor_secret, False, "mock")
    with pytest.raises(ValueError):
        create_app(production, sms_sender=DevelopmentSmsSender())


def test_missing_sdk_and_failed_delivery_never_issue_a_valid_code(environment):
    store, _, settings, _ = environment
    production = Settings(settings.database, settings.cursor_secret)
    def unavailable(*_args):
        raise RuntimeError("provider echo MUST NOT APPEAR")
    for sender in (None, SdkSmsSender(unavailable)):
        with TestClient(create_app(production, clock=store.clock, sms_sender=sender)) as client:
            reply = client.post("/v1/auth/code", json={"phone": PHONE, "purpose": "login"})
            assert reply.status_code == 503 and "MUST NOT APPEAR" not in reply.text
    with store.transaction() as db:
        assert db.execute("SELECT count(*) FROM sms_challenges").fetchone()[0] == 0


def test_request_bounds_duplicates_types_and_no_echo(environment):
    client = environment[3]
    for raw in ('{"phone":"13800000001","phone":"13900000001","purpose":"login"}',
                '{"phone":13800000001,"purpose":"login"}',
                '{"phone":"13800000001","purpose":"login","secret":"never echo me"}',
                '{"phone":"13800000001","purpose":"login"' , '[' * 2000):
        reply = client.post("/v1/auth/code", content=raw, headers={"Content-Type": "application/json"})
        assert reply.status_code == 400 and "never echo me" not in reply.text
        assert reply.headers["cache-control"] == "no-store"
    assert client.post("/v1/auth/code", content="x" * 4097, headers={"Content-Type": "application/json"}).status_code == 413


def test_schema_v1_migration_preserves_existing_data(tmp_path):
    path = tmp_path / "v1.sqlite"
    with sqlite3.connect(path) as db:
        db.executescript(SCHEMA + f"PRAGMA application_id={APPLICATION_ID}; PRAGMA user_version=1;")
        db.execute("INSERT INTO accounts(id,name) VALUES('legacy','Legacy')")
    store = Store(path)
    store.initialize()
    store.initialize()
    with store.transaction() as db:
        assert db.execute("SELECT name FROM accounts WHERE id='legacy'").fetchone()[0] == "Legacy"
        assert db.execute("PRAGMA user_version").fetchone()[0] == SCHEMA_VERSION


def test_limits_and_expiry_survive_restart(environment):
    store, clock, settings, client = environment
    tokens, _ = register(client)
    for index in range(4):
        clock[0] += 61
        challenge(client, "login")
    with TestClient(create_app(settings, clock=store.clock)) as restarted:
        clock[0] += 61
        assert restarted.post("/v1/auth/code", json={"phone": PHONE, "purpose": "login"}).status_code == 429
        clock[0] += 30 * 86400
        assert restarted.post("/v1/auth/refresh", json={"refresh_token": tokens["refresh_token"]}).status_code == 401

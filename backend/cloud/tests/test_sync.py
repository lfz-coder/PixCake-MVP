import copy
import hashlib
from concurrent.futures import ThreadPoolExecutor

import pytest
from fastapi.testclient import TestClient

from pixcake_cloud.api import create_app
from pixcake_cloud.config import Settings
from pixcake_cloud.store import Store
from pixcake_cloud.sync_models import CHUNK_BYTES


@pytest.fixture
def env(tmp_path):
    clock = [2_000_000_000]
    settings = Settings(tmp_path / "sync.sqlite", "test-sync-secret-with-at-least-32-bytes")
    store = Store(settings.database, clock=lambda: clock[0])
    store.initialize()
    owner = store.create_account("Owner")
    other = store.create_account("Other")
    headers = {"Authorization": "Bearer " + store.issue_session(owner["account_id"])}
    other_headers = {"Authorization": "Bearer " + store.issue_session(other["account_id"])}
    with TestClient(create_app(settings, clock=store.clock)) as client:
        yield store, clock, client, owner, other, headers, other_headers


def draft(space, content=b"source-content", *, project="", base=0, op="a"):
    chunks = [content[i:i + CHUNK_BYTES] for i in range(0, len(content), CHUNK_BYTES)]
    digests = [hashlib.sha256(chunk).hexdigest() for chunk in chunks]
    return {"operation_id": op * 64, "space_id": space, "project_id": project, "base_revision": base,
            "manifest": {"format": 1, "name": "Photo project", "files": [
                {"key": "f00000000", "kind": "original", "size": len(content), "chunks": digests}],
                "photos": [{"source": "f00000000", "name": "image.bmp", "rating": 4, "flag": "选取", "color": "红",
                            "items": [{"edit": "", "layer": ""}]}]}}, dict(zip(digests, chunks))


def upload(client, headers, body, chunks):
    reply = client.post("/v1/sync/uploads", json=body, headers=headers)
    assert reply.status_code == 200, reply.text
    value = reply.json()
    for digest in value["missing"]:
        assert client.post(f'/v1/sync/uploads/{value["upload_id"]}/chunks/{digest}', content=chunks[digest],
                           headers={**headers, "Content-Type": "application/octet-stream"}).status_code == 204
    return value


def commit(client, headers, upload_id):
    return client.post(f"/v1/sync/uploads/{upload_id}/commit", json={}, headers=headers)


def test_atomic_upload_download_resume_and_idempotence(env):
    store, _, client, owner, _, headers, _ = env
    body, chunks = draft(owner["personal_space_id"], b"a" * CHUNK_BYTES + b"b" * 123)
    started = client.post("/v1/sync/uploads", json=body, headers=headers).json()
    assert commit(client, headers, started["upload_id"]).json()["error"]["code"] == "incomplete_upload"
    assert client.get(f'/v1/spaces/{owner["personal_space_id"]}/projects', headers=headers).json()["items"] == []
    digest = next(iter(chunks))
    assert client.post(f'/v1/sync/uploads/{started["upload_id"]}/chunks/{digest}', content=chunks[digest],
                       headers={**headers, "Content-Type": "application/octet-stream"}).status_code == 204
    # Recreate the service over the same DB/object directory: acknowledged chunks survive restart.
    with TestClient(create_app(Settings(store.database, "test-sync-secret-with-at-least-32-bytes"), clock=store.clock)) as restarted:
        resumed = restarted.post("/v1/sync/uploads", json=body, headers=headers).json()
        assert resumed["upload_id"] == started["upload_id"] and digest not in resumed["missing"]
    finished = upload(client, headers, body, chunks)
    result = commit(client, headers, finished["upload_id"])
    assert result.status_code == 200 and result.json()["revision"] == 1
    assert commit(client, headers, finished["upload_id"]).json() == result.json()
    path = f'/v1/sync/projects/{result.json()["project_id"]}/revisions/1'
    assert client.get(path, headers=headers).json()["manifest"] == body["manifest"]
    for digest, content in chunks.items():
        assert client.get(path + "/chunks/" + digest, headers=headers).content == content
    assert client.post("/v1/sync/uploads", json=body, headers=headers).json()["revision"] == 1


def test_concurrent_revision_conflict_preserves_versions(env):
    _, _, client, owner, _, headers, _ = env
    body, chunks = draft(owner["personal_space_id"])
    first = upload(client, headers, body, chunks)
    assert commit(client, headers, first["upload_id"]).status_code == 200
    next_ids = []
    for op in ("b", "c"):
        body, chunks = draft(owner["personal_space_id"], op.encode(), project=first["project_id"], base=1, op=op)
        next_ids.append(upload(client, headers, body, chunks)["upload_id"])
    with ThreadPoolExecutor(2) as workers:
        responses = list(workers.map(lambda id: commit(client, headers, id), next_ids))
    assert sorted(r.status_code for r in responses) == [200, 409]
    assert client.get(f'/v1/sync/projects/{first["project_id"]}/revisions/1', headers=headers).status_code == 200
    assert client.get(f'/v1/sync/projects/{first["project_id"]}/revisions/2', headers=headers).status_code == 200


def test_ownership_revocation_read_write_separation_and_expiry(env):
    store, clock, client, owner, other, headers, other_headers = env
    team = store.create_team(owner["account_id"], "Team")
    body, chunks = draft(team)
    item = upload(client, headers, body, chunks)
    assert commit(client, other_headers, item["upload_id"]).status_code == 403
    store.set_permissions(owner["account_id"], team, ["read"])
    assert commit(client, headers, item["upload_id"]).status_code == 403
    store.set_permissions(owner["account_id"], team, ["write"])
    assert commit(client, headers, item["upload_id"]).status_code == 200
    path = f'/v1/sync/projects/{item["project_id"]}/revisions/1'
    assert client.get(path, headers=headers).status_code == 403
    store.set_permissions(other["account_id"], team, ["read"])
    assert client.get(path, headers=other_headers).status_code == 200
    store.revoke_membership(other["account_id"], team)
    assert client.get(path, headers=other_headers).status_code == 403
    body, _ = draft(team, op="d")
    pending = client.post("/v1/sync/uploads", json=body, headers=headers).json()
    clock[0] += 7 * 86400
    fresh = {"Authorization": "Bearer " + store.issue_session(owner["account_id"])}
    assert commit(client, fresh, pending["upload_id"]).status_code == 410


def test_corrupt_chunks_path_traversal_duplicate_keys_and_limits(env):
    store, _, client, owner, _, headers, _ = env
    body, chunks = draft(owner["personal_space_id"])
    item = client.post("/v1/sync/uploads", json=body, headers=headers).json()
    digest = next(iter(chunks))
    path = f'/v1/sync/uploads/{item["upload_id"]}/chunks/{digest}'
    binary = {**headers, "Content-Type": "application/octet-stream"}
    assert client.post(path, content=b"tampered", headers=binary).status_code == 400
    assert client.post(path, content=b"x" * (CHUNK_BYTES + 1), headers=binary).status_code == 413
    assert client.post(path, content=chunks[digest], headers=binary).status_code == 204
    (store.database.with_suffix(".objects") / item["upload_id"] / digest).write_bytes(b"corrupt")
    assert commit(client, headers, item["upload_id"]).status_code == 409
    for key, value in [("key", "../escape"), ("size", True), ("chunks", ["https://outside/"])]:
        bad = copy.deepcopy(body)
        bad["manifest"]["files"][0][key] = value
        assert client.post("/v1/sync/uploads", json=bad, headers=headers).status_code == 400
    bad = copy.deepcopy(body)
    bad["manifest"]["photos"][0]["source"] = "f99999999"
    assert client.post("/v1/sync/uploads", json=bad, headers=headers).status_code == 400
    bad = copy.deepcopy(body)
    bad["manifest"]["format"] = True
    assert client.post("/v1/sync/uploads", json=bad, headers=headers).status_code == 400
    assert client.post("/v1/sync/uploads", content=b'{"a":1,"a":2}',
                       headers={**headers, "Content-Type": "application/json"}).status_code == 400
    assert client.post("/v1/sync/uploads?unexpected=1", json=body, headers=headers).status_code == 400


def test_pending_upload_quota_and_operation_key_binding(env):
    _, _, client, owner, _, headers, _ = env
    for op in "abcd":
        body, _ = draft(owner["personal_space_id"], op=op)
        assert client.post("/v1/sync/uploads", json=body, headers=headers).status_code == 200
    body["manifest"]["name"] = "Changed"
    assert client.post("/v1/sync/uploads", json=body, headers=headers).status_code == 409
    body["operation_id"] = "e" * 64
    assert client.post("/v1/sync/uploads", json=body, headers=headers).status_code == 413


def test_abort_is_idempotent_and_never_removes_published_versions(env):
    store, _, client, owner, other, headers, other_headers = env
    body, chunks = draft(owner["personal_space_id"])
    pending = upload(client, headers, body, chunks)
    path = "/v1/sync/uploads/" + pending["upload_id"]
    assert client.delete(path, headers=other_headers).status_code == 403
    assert client.delete(path, headers=headers).status_code == 204
    assert client.delete(path, headers=headers).status_code == 204
    assert not (store.database.with_suffix(".objects") / pending["upload_id"]).exists()
    finished = upload(client, headers, body, chunks)
    assert commit(client, headers, finished["upload_id"]).status_code == 200
    assert client.delete("/v1/sync/uploads/" + finished["upload_id"], headers=headers).status_code == 204
    assert client.get(f'/v1/sync/projects/{finished["project_id"]}/revisions/1', headers=headers).status_code == 200
    team = store.create_team(owner["account_id"], "Revoked team")
    body, chunks = draft(team, op="d")
    pending = upload(client, headers, body, chunks)
    path = "/v1/sync/uploads/" + pending["upload_id"]
    store.set_permissions(other["account_id"], team, ["write"])
    store.revoke_membership(owner["account_id"], team)
    assert commit(client, headers, pending["upload_id"]).status_code == 403
    assert client.delete(path).status_code == 401
    assert client.delete(path, headers=other_headers).status_code == 403
    assert client.delete(path, headers=headers).status_code == 204
    assert not (store.database.with_suffix(".objects") / pending["upload_id"]).exists()


def test_verification_concurrency_is_bounded(env):
    from pixcake_cloud.sync_service import SyncService
    from pixcake_cloud.sync_models import BeginUpload
    from pixcake_cloud.results import Code
    store, _, _, owner, _, headers, _ = env
    service = SyncService(store)
    body, _ = draft(owner["personal_space_id"])
    request = BeginUpload.model_validate(body)
    token = headers["Authorization"][7:]
    assert service.verification_slots.acquire(blocking=False)
    assert service.verification_slots.acquire(blocking=False)
    try:
        assert service.begin(token, request).error == Code.RATE_LIMITED
        assert service.commit(token, "up_" + "a" * 32).error == Code.RATE_LIMITED
    finally:
        service.verification_slots.release()
        service.verification_slots.release()
    assert service.begin(token, request).error is None

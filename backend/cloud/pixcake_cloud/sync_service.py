import hashlib
import json
import os
import re
import shutil
import stat
import threading
from pathlib import Path

from .results import Code, Result
from .store import Store, new_id
from .sync_models import Manifest, chunk_sizes


class SyncService:
    def __init__(self, store: Store):
        self.store = store
        self.root = store.database.with_suffix(".objects")
        self.root.mkdir(mode=0o700, exist_ok=True)
        if self.root.is_symlink() or not self.root.is_dir():
            raise RuntimeError("Invalid object directory")
        self.verification_slots = threading.BoundedSemaphore(2)

    def verify_bounded(self, operation):
        if not self.verification_slots.acquire(blocking=False):
            return Result(error=Code.RATE_LIMITED)
        try:
            return operation()
        finally:
            self.verification_slots.release()

    def principal(self, db, token, space_id, capability):
        principal = self.store.authenticate(db, token)
        if principal is None:
            return None, Code.UNAUTHORIZED
        row = db.execute("SELECT can_read,can_write FROM memberships WHERE account_id=? AND space_id=?",
                         (principal.account_id, space_id)).fetchone()
        if row is None or not row["can_" + capability]:
            return None, Code.FORBIDDEN
        return principal, None

    def upload(self, db, token, upload_id):
        if not re.fullmatch(r"up_[0-9a-f]{32}", upload_id):
            return None, Code.INVALID_REQUEST
        row = db.execute("SELECT * FROM uploads WHERE id=?", (upload_id,)).fetchone()
        principal = self.store.authenticate(db, token)
        if principal is None:
            return None, Code.UNAUTHORIZED
        if row is None or row["account_id"] != principal.account_id:
            return None, Code.FORBIDDEN
        _, error = self.principal(db, token, row["space_id"], "write")
        if error:
            return None, error
        if not row["committed_revision"] and row["expires_at"] <= int(self.store.clock()):
            return None, Code.TRANSFER_EXPIRED
        return row, None

    def object_path(self, upload_id, digest):
        folder = self.root / upload_id
        if folder.is_symlink():
            raise OSError("Invalid object directory")
        return folder / digest

    def read_chunk(self, upload_id, digest, size):
        path = self.object_path(upload_id, digest)
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            return None
        with os.fdopen(fd, "rb") as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size != size:
                return None
            data = source.read(size + 1)
        return data if len(data) == size and hashlib.sha256(data).hexdigest() == digest else None

    def begin(self, token, body):
        return self.verify_bounded(lambda: self._begin(token, body))

    def _begin(self, token, body):
        serialized = json.dumps(body.model_dump(), ensure_ascii=False, separators=(",", ":"), sort_keys=True)
        request_hash = hashlib.sha256(serialized.encode()).hexdigest()
        with self.store.transaction(write=True) as db:
            principal, error = self.principal(db, token, body.space_id, "write")
            if error:
                return Result(error=error)
            now = int(self.store.clock())
            expired = db.execute("SELECT id FROM uploads WHERE account_id=? AND committed_revision=0 AND expires_at<=?",
                                 (principal.account_id, now)).fetchall()
            for row in expired:
                folder = self.root / row["id"]
                if folder.exists() and not folder.is_symlink():
                    shutil.rmtree(folder)
                db.execute("DELETE FROM uploads WHERE id=?", (row["id"],))
            row = db.execute("SELECT * FROM uploads WHERE account_id=? AND operation_id=?",
                             (principal.account_id, body.operation_id)).fetchone()
            if row and row["request_hash"] != request_hash:
                return Result(error=Code.REVISION_CONFLICT)
            if row is None:
                if body.project_id:
                    project = db.execute("SELECT * FROM projects WHERE id=? AND space_id=?",
                                         (body.project_id, body.space_id)).fetchone()
                    if project is None:
                        return Result(error=Code.FORBIDDEN)
                    if project["revision"] != body.base_revision:
                        return Result(error=Code.REVISION_CONFLICT)
                size = sum(file.size for file in body.manifest.files)
                usage = db.execute("SELECT coalesce(sum(size_bytes),0), sum(committed_revision=0) FROM uploads WHERE account_id=?",
                                   (principal.account_id,)).fetchone()
                if usage[0] + size > 50 * 1024**3 or (usage[1] or 0) >= 4:
                    return Result(error=Code.QUOTA_EXCEEDED)
                upload_id = new_id("up")
                project_id = body.project_id or new_id("prj")
                db.execute("""INSERT INTO uploads(id,account_id,operation_id,request_hash,space_id,project_id,
                    base_revision,manifest,size_bytes,expires_at) VALUES(?,?,?,?,?,?,?,?,?,?)""",
                    (upload_id, principal.account_id, body.operation_id, request_hash, body.space_id, project_id,
                     body.base_revision, body.manifest.model_dump_json(), size, now + 7 * 86400))
                row = db.execute("SELECT * FROM uploads WHERE id=?", (upload_id,)).fetchone()
        sizes = chunk_sizes(body.manifest)
        missing = [digest for digest, size in sizes.items() if self.read_chunk(row["id"], digest, size) is None]
        with self.store.transaction() as db:
            row, error = self.upload(db, token, row["id"])
            if error:
                return Result(error=error)
            return Result({"api_version": 1, "upload_id": row["id"], "project_id": row["project_id"],
                           "revision": row["committed_revision"], "missing": missing})

    def put_chunk(self, token, upload_id, digest, data):
        if not re.fullmatch(r"[0-9a-f]{64}", digest):
            return Result(error=Code.INVALID_REQUEST)
        with self.store.transaction(write=True) as db:
            row, error = self.upload(db, token, upload_id)
            if error:
                return Result(error=error)
            sizes = chunk_sizes(Manifest.model_validate_json(row["manifest"]))
            if digest not in sizes or len(data) != sizes[digest] or hashlib.sha256(data).hexdigest() != digest:
                return Result(error=Code.INVALID_REQUEST)
            if row["committed_revision"]:
                # A published revision is immutable, including in the face of disk corruption.
                return Result(error=Code.REVISION_CONFLICT)
            path = self.object_path(upload_id, digest)
            path.parent.mkdir(mode=0o700, exist_ok=True)
            self.sync_directory(self.root)
            temporary = path.with_suffix(".tmp")
            fd = os.open(temporary, os.O_CREAT | os.O_WRONLY | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
            with os.fdopen(fd, "wb") as output:
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, path)
            self.sync_directory(path.parent)
        return Result()

    @staticmethod
    def sync_directory(path: Path):
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)

    def commit(self, token, upload_id):
        return self.verify_bounded(lambda: self._commit(token, upload_id))

    def _commit(self, token, upload_id):
        # Large disk verification happens outside the write transaction. Objects
        # have content-addressed names; writers can only install those exact bytes.
        with self.store.transaction() as db:
            row, error = self.upload(db, token, upload_id)
            if error:
                return Result(error=error)
            manifest = Manifest.model_validate_json(row["manifest"])
        if not row["committed_revision"]:
            for digest, size in chunk_sizes(manifest).items():
                if self.read_chunk(upload_id, digest, size) is None:
                    return Result(error=Code.INCOMPLETE_UPLOAD)
        with self.store.transaction(write=True) as db:
            row, error = self.upload(db, token, upload_id)
            if error:
                return Result(error=error)
            if row["committed_revision"]:
                return Result({"api_version": 1, "project_id": row["project_id"], "revision": row["committed_revision"]})
            project = db.execute("SELECT revision FROM projects WHERE id=?", (row["project_id"],)).fetchone()
            if (project is None and row["base_revision"] != 0) or (project and project["revision"] != row["base_revision"]):
                return Result(error=Code.REVISION_CONFLICT)
            revision = row["base_revision"] + 1
            if project is None:
                db.execute("INSERT INTO projects(id,space_id,name,revision,updated_at,size_bytes) VALUES(?,?,?,?,?,?)",
                           (row["project_id"], row["space_id"], manifest.name, revision, int(self.store.clock()), row["size_bytes"]))
            else:
                db.execute("UPDATE projects SET name=?,revision=?,updated_at=?,size_bytes=? WHERE id=?",
                           (manifest.name, revision, int(self.store.clock()), row["size_bytes"], row["project_id"]))
            db.execute("INSERT INTO project_versions VALUES(?,?,?)", (row["project_id"], revision, upload_id))
            db.execute("UPDATE uploads SET committed_revision=? WHERE id=?", (revision, upload_id))
            db.execute("UPDATE spaces SET projects_revision=projects_revision+1 WHERE id=?", (row["space_id"],))
            return Result({"api_version": 1, "project_id": row["project_id"], "revision": revision})

    def download(self, token, project_id, revision, digest=None):
        if not re.fullmatch(r"prj_[0-9a-f]{32}", project_id) or not 1 <= revision <= 2**63 - 1:
            return Result(error=Code.INVALID_REQUEST)
        if digest is not None and not re.fullmatch(r"[0-9a-f]{64}", digest):
            return Result(error=Code.INVALID_REQUEST)
        with self.store.transaction() as db:
            principal = self.store.authenticate(db, token)
            if principal is None:
                return Result(error=Code.UNAUTHORIZED)
            project = db.execute("SELECT space_id FROM projects WHERE id=?", (project_id,)).fetchone()
            if project is None:
                return Result(error=Code.FORBIDDEN)
            _, error = self.principal(db, token, project["space_id"], "read")
            if error:
                return Result(error=error)
            row = db.execute("SELECT u.id,u.manifest FROM project_versions v JOIN uploads u ON u.id=v.upload_id "
                             "WHERE v.project_id=? AND v.revision=?", (project_id, revision)).fetchone()
            if row is None:
                return Result(error=Code.NOT_FOUND)
            manifest = Manifest.model_validate_json(row["manifest"])
            if digest is None:
                return Result({"api_version": 1, "project_id": project_id, "space_id": project["space_id"],
                               "revision": revision, "manifest": manifest.model_dump()})
            size = chunk_sizes(manifest).get(digest)
            if size is None:
                return Result(error=Code.FORBIDDEN)
            data = self.read_chunk(row["id"], digest, size)
            return Result(data) if data is not None else Result(error=Code.STORAGE_UNAVAILABLE)

    def abort(self, token, upload_id):
        if not re.fullmatch(r"up_[0-9a-f]{32}", upload_id):
            return Result(error=Code.INVALID_REQUEST)
        with self.store.transaction(write=True) as db:
            principal = self.store.authenticate(db, token)
            if principal is None:
                return Result(error=Code.UNAUTHORIZED)
            row = db.execute("SELECT * FROM uploads WHERE id=?", (upload_id,)).fetchone()
            if row is None:
                return Result()  # Idempotent after a lost abort response.
            if row["account_id"] != principal.account_id:
                return Result(error=Code.FORBIDDEN)
            # Revocation must stop publishing, but cannot prevent an owner from
            # releasing their own uncommitted bytes. Published data is immutable.
            if row["committed_revision"]:
                return Result()  # Clearing a task never deletes a published project/version.
            folder = self.root / upload_id
            if folder.exists() and not folder.is_symlink():
                shutil.rmtree(folder)
            db.execute("DELETE FROM uploads WHERE id=?", (upload_id,))
        return Result()

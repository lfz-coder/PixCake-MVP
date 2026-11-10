from .cursors import CursorCodec, Position
from .results import Code, Result
from .store import PERMISSIONS, Store
from .auth import AuthService


class DirectoryService:
    def __init__(self, store: Store, cursors: CursorCodec):
        self.store = store
        self.cursors = cursors

    def account(self, token: str) -> Result:
        with self.store.transaction() as connection:
            principal = self.store.authenticate(connection, token)
            if principal is None:
                return Result(error=Code.UNAUTHORIZED)
            return Result({"api_version": 1, "account_id": principal.account_id,
                           "session_expires_at": principal.expires_at})

    def logout(self, token: str) -> Result:
        with self.store.transaction(write=True) as connection:
            principal = self.store.authenticate(connection, token)
            if principal is None:
                return Result(error=Code.UNAUTHORIZED)
            connection.execute("UPDATE sessions SET revoked=1 WHERE token_hash=?", (principal.token_hash,))
            family = connection.execute("SELECT family_id FROM sessions WHERE token_hash=?", (principal.token_hash,)).fetchone()[0]
            if family:
                AuthService.revoke_family(connection, family)
        return Result()

    def _page(self, token: str, limit: int, cursor: str, space_id: str | None) -> Result:
        if type(limit) is not int or not 1 <= limit <= 100:
            return Result(error=Code.INVALID_REQUEST)
        with self.store.transaction() as connection:
            principal = self.store.authenticate(connection, token)
            if principal is None:
                return Result(error=Code.UNAUTHORIZED)
            revisions = [principal.directory_revision]
            if space_id is not None:
                space = connection.execute("""SELECT s.projects_revision,m.can_read FROM spaces s
                    JOIN memberships m ON m.space_id=s.id WHERE s.id=? AND m.account_id=?""",
                    (space_id, principal.account_id)).fetchone()
                if space is None or not space["can_read"]:
                    return Result(error=Code.FORBIDDEN)
                revisions.append(space["projects_revision"])
            binding = {"session": principal.token_hash, "account": principal.account_id,
                       "directory": "spaces" if space_id is None else "projects",
                       "space": space_id, "limit": limit}
            decoded = self.cursors.read(cursor, binding, revisions)
            if decoded.error:
                return decoded
            position = decoded.value
            if space_id is None:
                rows = connection.execute("""SELECT s.id,s.name,s.kind,m.can_read,m.can_write,m.can_manage
                    FROM memberships m JOIN spaces s ON s.id=m.space_id
                    WHERE m.account_id=? AND s.id>? ORDER BY s.id LIMIT ?""",
                    (principal.account_id, position.after, limit + 1)).fetchall()
                items = [{"id": row["id"], "name": row["name"], "kind": row["kind"],
                          "permissions": [p for p in PERMISSIONS if row["can_" + p]]} for row in rows[:limit]]
            else:
                rows = connection.execute("""SELECT id,space_id,name,revision,updated_at,size_bytes
                    FROM projects WHERE space_id=? AND id>? ORDER BY id LIMIT ?""",
                    (space_id, position.after, limit + 1)).fetchall()
                items = [dict(row) for row in rows[:limit]]
            next_cursor = self.cursors.write(binding, revisions, Position(items[-1]["id"], position.expires)) \
                if len(rows) > limit else ""
            return Result({"api_version": 1, "items": items, "next_cursor": next_cursor})

    def spaces(self, token: str, limit=100, cursor="") -> Result:
        return self._page(token, limit, cursor, None)

    def projects(self, token: str, space_id: str, limit=100, cursor="") -> Result:
        return self._page(token, limit, cursor, space_id)

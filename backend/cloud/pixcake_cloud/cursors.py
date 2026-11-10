import hashlib
import time
from dataclasses import dataclass
from typing import Callable

from itsdangerous import BadData, URLSafeSerializer

from .results import Code, Result


@dataclass(frozen=True)
class Position:
    after: str
    expires: int


class CursorCodec:
    def __init__(self, secret: str, clock: Callable = time.time):
        self._serializer = URLSafeSerializer(
            secret, salt="pixcake-cloud-directory-v1",
            signer_kwargs={"digest_method": hashlib.sha256},
        )
        self._clock = clock

    def read(self, cursor: str, binding: dict, revision: list[int]) -> Result:
        now = int(self._clock())
        if not cursor:
            return Result(Position("", now + 300))
        if not cursor.isascii() or len(cursor) > 4096:
            return Result(error=Code.INVALID_CURSOR)
        try:
            payload = self._serializer.loads(cursor)
        except BadData:
            return Result(error=Code.INVALID_CURSOR)
        if (not isinstance(payload, dict) or payload.get("binding") != binding
                or payload.get("version") != 1 or type(payload.get("expires")) is not int
                or not isinstance(payload.get("after"), str) or len(payload["after"]) > 64
                or not isinstance(payload.get("revision"), list)):
            return Result(error=Code.INVALID_CURSOR)
        if payload["expires"] <= now:
            return Result(error=Code.CURSOR_EXPIRED)
        if payload["revision"] != revision:
            return Result(error=Code.DIRECTORY_CHANGED)
        return Result(Position(payload["after"], payload["expires"]))

    def write(self, binding: dict, revision: list[int], position: Position) -> str:
        return self._serializer.dumps({"version": 1, "binding": binding,
            "revision": revision, "after": position.after, "expires": position.expires})

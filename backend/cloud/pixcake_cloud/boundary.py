import asyncio
import json
import logging
import sqlite3

from starlette.responses import JSONResponse

from .results import Code, ERRORS

logger = logging.getLogger("pixcake.cloud")


def unique_fields(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("Duplicate field")
        value[key] = item
    return value


def failure(code: Code, *, status=None):
    default_status, message = ERRORS[code]
    headers = {"Cache-Control": "no-store"}
    if code == Code.UNAUTHORIZED:
        headers["WWW-Authenticate"] = "Bearer"
    return JSONResponse({"api_version": 1, "error": {"code": code.value, "message": message}},
                        status_code=status or default_status, headers=headers)


class RequestBoundary:
    """Bound wire envelopes and prevent exception/input details reaching logs."""

    def __init__(self, app):
        self.app = app

    async def __call__(self, scope, receive, send):
        if scope["type"] != "http":
            await self.app(scope, receive, send)
            return
        headers = scope.get("headers", [])
        if len(headers) > 128 or sum(len(k) + len(v) for k, v in headers) > 32768:
            await failure(Code.REQUEST_TOO_LARGE, status=431)(scope, receive, send)
            return
        if len(scope.get("query_string", b"")) > 8192 or len(scope.get("raw_path", b"")) > 1024:
            await failure(Code.REQUEST_TOO_LARGE)(scope, receive, send)
            return
        authorization = [value for key, value in headers if key.lower() == b"authorization"]
        content_lengths = [value for key, value in headers if key.lower() == b"content-length"]
        auth = scope["path"].startswith("/v1/auth/")
        syncing = scope["path"].startswith("/v1/sync/")
        posting = scope["method"] == "POST" and (auth or syncing)
        binary = syncing and "/chunks/" in scope["path"]
        maximum = 4096 if auth else 1024 * 1024 if binary else 2 * 1024 * 1024
        if syncing and scope.get("query_string"):
            await failure(Code.INVALID_REQUEST)(scope, receive, send)
            return
        if (len(authorization) > 1 or len(content_lengths) > 1
                or any(key.lower() == b"transfer-encoding" for key, _ in headers)
                or any(not value.isdigit() or len(value) > 8 for value in content_lengths)
                or (not posting and any(value != b"0" for value in content_lengths))):
            await failure(Code.INVALID_REQUEST)(scope, receive, send)
            return
        if posting:
            types = [v.split(b";")[0].strip().lower() for k, v in headers if k.lower() == b"content-type"]
            if types != [b"application/octet-stream" if binary else b"application/json"] or scope.get("query_string"):
                await failure(Code.INVALID_REQUEST)(scope, receive, send)
                return
            if any(int(value) > maximum for value in content_lengths):
                await failure(Code.REQUEST_TOO_LARGE)(scope, receive, send)
                return

            async def collect():
                body = bytearray()
                while True:
                    message = await receive()
                    if message["type"] != "http.request":
                        raise ValueError("Disconnected")
                    body.extend(message.get("body", b""))
                    if len(body) > maximum:
                        raise OverflowError()
                    if not message.get("more_body", False):
                        return bytes(body)

            try:
                body = await asyncio.wait_for(collect(), timeout=5)
                if not binary:
                    data = json.loads(body.decode("utf-8"), object_pairs_hook=unique_fields)
                    if not isinstance(data, dict):
                        raise ValueError("Expected object")
                    if auth and any(not isinstance(v, str) for v in data.values()):
                        raise ValueError("Expected flat string fields")
                    stack, nodes = [(data, 0)], 0
                    while stack:
                        value, depth = stack.pop()
                        nodes += 1
                        if depth > 10 or nodes > 100000:
                            raise ValueError("JSON structure limit")
                        if isinstance(value, str):
                            if len(value.encode("utf-8")) > 4096:
                                raise ValueError("String size")
                        elif isinstance(value, dict):
                            stack.extend((v, depth + 1) for v in value.values())
                        elif isinstance(value, list):
                            stack.extend((v, depth + 1) for v in value)
                if content_lengths and len(body) != int(content_lengths[0]):
                    raise ValueError("Length mismatch")
            except (ValueError, RecursionError, TimeoutError):
                await failure(Code.INVALID_REQUEST)(scope, receive, send)
                return
            except OverflowError:
                await failure(Code.REQUEST_TOO_LARGE)(scope, receive, send)
                return

            async def replay():
                return {"type": "http.request", "body": body, "more_body": False}

            receive = replay

        started = False

        async def bounded_send(message):
            nonlocal started
            if message["type"] == "http.response.start":
                started = True
                message = dict(message)
                message["headers"] = [(k, v) for k, v in message.get("headers", [])
                                      if k.lower() != b"cache-control"] + [(b"cache-control", b"no-store")]
            await send(message)

        try:
            await self.app(scope, receive, bounded_send)
        except Exception as error:
            # Deliberately exclude URL, query, headers, traceback and str(error).
            logger.warning("cloud.request.failed category=%s", type(error).__name__)
            if not started:
                code = Code.STORAGE_UNAVAILABLE if isinstance(error, (sqlite3.Error, OSError)) else Code.INTERNAL_ERROR
                await failure(code)(scope, receive, send)

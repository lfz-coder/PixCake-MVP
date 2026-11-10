"""TLS fault fixtures for the isolated C++ integration runner, never production."""
import asyncio
import gzip

from fastapi import Response

from pixcake_cloud.api import create_app
from pixcake_cloud.config import Settings


def create_fixture_app():
    app = create_app(Settings.from_environment())
    hits = 0
    directory_mode = "normal"

    @app.middleware("http")
    async def directory_fault(request, call_next):
        if request.url.path == "/v1/spaces":
            if directory_mode == "offline":
                return Response(status_code=503)
            if directory_mode == "slow-pages":
                await asyncio.sleep(0.12)
        return await call_next(request)

    @app.post("/__fixture__/mode/{mode}")
    async def mode(mode: str):
        nonlocal directory_mode
        if mode not in ("normal", "offline", "slow-pages"):
            return Response(status_code=400)
        directory_mode = mode
        return Response(status_code=204)

    @app.get("/__fixture__/redirect")
    async def redirect():
        return Response(status_code=302, headers={"Location": "/__fixture__/followed"})

    @app.get("/__fixture__/followed")
    async def followed():
        nonlocal hits
        hits += 1
        return Response(b"followed")

    @app.get("/__fixture__/hits")
    async def read_hits():
        return Response(str(hits))

    @app.get("/__fixture__/body")
    async def body():
        return Response(b"x" * (256 * 1024))

    @app.get("/__fixture__/gzip")
    async def compressed():
        return Response(gzip.compress(b"x" * (256 * 1024)), headers={"Content-Encoding": "gzip"})

    @app.get("/__fixture__/headers")
    async def headers():
        return Response(b"ok", headers={f"X-Test-{i}": "value" for i in range(150)})

    @app.get("/__fixture__/slow")
    async def slow():
        await asyncio.sleep(0.4)
        return Response(b"late")

    @app.get("/__fixture__/bad-json")
    async def bad_json():
        return Response(b'{"api_version":1,"api_version":1,"items":[],"next_cursor":""}', media_type="application/json")

    return app

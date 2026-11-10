"""Run the actual loopback HTTP server against an isolated temporary database.

No externally supplied credentials or user files are read. The spawned server
is always terminated, including when an assertion fails.
"""
import os
import secrets
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import httpx

from pixcake_cloud.store import Store


def main():
    with tempfile.TemporaryDirectory(prefix="pixcake-cloud-http-") as directory:
        database = Path(directory) / "cloud.sqlite"
        store = Store(database)
        store.initialize()
        alice = store.create_account("HTTP Alice")
        bob = store.create_account("HTTP Bob")
        team = store.create_team(alice["account_id"], "真实 HTTP 团队目录")
        expected = {store.create_project(team, f"项目 {i}") for i in range(3)}
        token = store.issue_session(alice["account_id"])
        other = store.issue_session(bob["account_id"])
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        env = {**os.environ, "PIXCAKE_CLOUD_CURSOR_SECRET": secrets.token_urlsafe(32)}
        process = subprocess.Popen([sys.executable, "-m", "pixcake_cloud", "--db", str(database),
                                    "serve", "--port", str(port)], env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            with httpx.Client(base_url=f"http://127.0.0.1:{port}", timeout=3,
                              trust_env=False, follow_redirects=False) as client:
                deadline = time.monotonic() + 10
                while True:
                    if process.poll() is not None:
                        raise RuntimeError("Loopback server exited before becoming ready.")
                    try:
                        if client.get("/healthz").status_code == 200:
                            break
                    except httpx.TransportError:
                        pass
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Loopback server did not become ready.")
                    time.sleep(0.05)
                assert client.get("/v1/spaces").status_code == 401
                headers = {"Authorization": f"Bearer {token}"}
                first = client.get("/v1/spaces", headers=headers)
                assert first.status_code == 200 and len(first.json()["items"]) == 2
                assert first.headers["cache-control"] == "no-store"
                cursor = ""
                seen = set()
                for _ in range(3):
                    page = client.get(f"/v1/spaces/{team}/projects", headers=headers,
                                      params={"limit": 1, "cursor": cursor})
                    assert page.status_code == 200
                    seen.update(item["id"] for item in page.json()["items"])
                    cursor = page.json()["next_cursor"]
                assert seen == expected and not cursor
                assert client.get(f"/v1/spaces/{team}/projects", headers={"Authorization": f"Bearer {other}"}).status_code == 403
                assert client.delete("/v1/session", headers=headers).status_code == 204
                assert client.get("/v1/spaces", headers=headers).status_code == 401
            print("HTTP smoke passed: live server, identity isolation, 3 pages, logout persistence.")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()

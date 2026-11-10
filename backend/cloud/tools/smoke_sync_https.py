"""Verify actual C++ upload/download and restored rendering in two isolated libraries."""
import os
import secrets
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import httpx

from pixcake_cloud.store import Store
from tools.smoke_cpp_https import certificates


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Supply test_cloud_sync.")
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="pixcake-sync-https-") as directory:
        root = Path(directory)
        certificates(root)
        store = Store(root / "sync.sqlite")
        store.initialize()
        account = store.create_account("Sync test")
        token = store.issue_session(account["account_id"])
        fixture = root / "fixture.token"
        with open(fixture, "x", opener=lambda path, flags: os.open(path, flags, 0o600)) as output:
            output.write(token + "\n" + account["personal_space_id"] + "\n")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        origin = f"https://127.0.0.1:{port}"
        env = {**os.environ, "PIXCAKE_CLOUD_DB": str(store.database),
               "PIXCAKE_CLOUD_CURSOR_SECRET": secrets.token_urlsafe(32), "PIXCAKE_CLOUD_MODE": "production",
               "PIXCAKE_SMS_PROVIDER": "none"}
        server = subprocess.Popen([sys.executable, "-m", "uvicorn", "pixcake_cloud.api:create_environment_app", "--factory",
            "--host", "127.0.0.1", "--port", str(port), "--ssl-certfile", str(root / "server.pem"),
            "--ssl-keyfile", str(root / "server.key"), "--http", "h11", "--loop", "asyncio", "--ws", "none",
            "--no-access-log", "--no-proxy-headers", "--no-server-header", "--limit-concurrency", "64"],
            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            with httpx.Client(base_url=origin, verify=ssl.create_default_context(cafile=str(root / "ca.pem")),
                              trust_env=False, timeout=2) as client:
                deadline = time.monotonic() + 10
                while True:
                    if server.poll() is not None:
                        raise RuntimeError("Sync TLS server exited before readiness.")
                    try:
                        if client.get("/healthz").status_code == 200:
                            break
                    except httpx.TransportError:
                        pass
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Sync TLS server did not become ready.")
                    time.sleep(0.05)
            subprocess.run([str(executable), origin, str(root / "ca.pem"), str(fixture)], check=True, timeout=120)
            with store.transaction() as db:
                assert db.execute("SELECT count(*) FROM projects WHERE revision>0").fetchone()[0] == 2
                assert db.execute("SELECT count(*) FROM project_versions").fetchone()[0] == 3
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)


if __name__ == "__main__":
    main()

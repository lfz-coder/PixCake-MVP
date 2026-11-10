"""Real TLS + desktop registration/login/reset. Temporary data, no keychain writes."""
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
        raise SystemExit("Supply the development desktop executable.")
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="pixcake-auth-https-") as directory:
        root = Path(directory)
        certificates(root)
        store = Store(root / "auth.sqlite")
        store.initialize()
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        origin = f"https://127.0.0.1:{port}"
        env = {key: value for key, value in os.environ.items() if not key.startswith("PIXCAKE_ASSISTANT_")}
        env.update(PIXCAKE_CLOUD_DB=str(store.database), PIXCAKE_CLOUD_CURSOR_SECRET=secrets.token_urlsafe(32),
                   PIXCAKE_CLOUD_MODE="development", PIXCAKE_SMS_PROVIDER="mock", PIXCAKE_CLOUD_URL=origin,
                   PIXCAKE_CLOUD_CA_FILE=str(root / "ca.pem"), QT_QPA_PLATFORM="offscreen")
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
                        raise RuntimeError("Auth TLS server exited before readiness.")
                    try:
                        if client.get("/healthz").status_code == 200:
                            break
                    except httpx.TransportError:
                        pass
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Auth TLS server did not become ready.")
                    time.sleep(0.05)
            result = subprocess.run([str(executable), "--smoke", "--smoke-auth"], env=env,
                                    capture_output=True, text=True, timeout=60)
            if result.returncode:
                # Desktop diagnostics contain only generic local messages.
                raise RuntimeError(f"Desktop auth smoke failed ({result.returncode}): {result.stderr[-2000:]}")
            with store.transaction() as db:
                assert db.execute("SELECT count(*) FROM credentials").fetchone()[0] == 1
                assert db.execute("SELECT count(*) FROM spaces WHERE kind='personal'").fetchone()[0] == 1
                assert db.execute("SELECT count(*) FROM sessions WHERE revoked=0").fetchone()[0] == 0
                assert db.execute("SELECT count(*) FROM refresh_sessions WHERE revoked=0").fetchone()[0] == 0
            print("Desktop HTTPS auth passed: random OTP auto-fill, registration, password/SMS login, reset, cloud and logout.")
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)


if __name__ == "__main__":
    main()

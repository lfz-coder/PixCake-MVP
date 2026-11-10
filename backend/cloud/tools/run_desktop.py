"""Explicit local development launcher; never a production server or SMS provider."""
import argparse
import os
import secrets
import ssl
import subprocess
import sys
import time
from pathlib import Path

import httpx

from pixcake_cloud.store import Store
from tools.smoke_cpp_https import certificates


def main():
    parser = argparse.ArgumentParser(description="Run desktop registration with local mock SMS over verified TLS")
    parser.add_argument("executable", type=Path)
    parser.add_argument("--state-dir", type=Path, default=Path(".state/development"))
    parser.add_argument("--port", type=int, default=8448)
    parser.add_argument("--smoke", action="store_true", help="Isolated test UI; use an empty throwaway state directory")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    if not 1024 <= args.port <= 65535:
        parser.error("Port must be between 1024 and 65535.")
    root = args.state_dir.resolve()
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    root.chmod(0o700)
    tls = root / "tls"
    tls.mkdir(exist_ok=True, mode=0o700)
    certificates(tls)
    store = Store(root / "cloud.sqlite")
    store.initialize()
    origin = f"https://127.0.0.1:{args.port}"
    env = {key: value for key, value in os.environ.items() if not key.startswith("PIXCAKE_ASSISTANT_")}
    env.update(PIXCAKE_CLOUD_DB=str(store.database), PIXCAKE_CLOUD_CURSOR_SECRET=secrets.token_urlsafe(32),
               PIXCAKE_CLOUD_MODE="development", PIXCAKE_SMS_PROVIDER="mock", PIXCAKE_CLOUD_URL=origin,
               PIXCAKE_CLOUD_CA_FILE=str(tls / "ca.pem"))
    if args.smoke:
        env["QT_QPA_PLATFORM"] = "offscreen"
    server = subprocess.Popen([sys.executable, "-m", "uvicorn", "pixcake_cloud.api:create_environment_app", "--factory",
        "--host", "127.0.0.1", "--port", str(args.port), "--ssl-certfile", str(tls / "server.pem"),
        "--ssl-keyfile", str(tls / "server.key"), "--http", "h11", "--loop", "asyncio", "--ws", "none",
        "--no-access-log", "--no-proxy-headers", "--no-server-header", "--limit-concurrency", "64"],
        env=env, stdout=subprocess.DEVNULL)
    try:
        with httpx.Client(base_url=origin, verify=ssl.create_default_context(cafile=str(tls / "ca.pem")),
                          trust_env=False, timeout=2) as client:
            deadline = time.monotonic() + 10
            while True:
                if server.poll() is not None:
                    raise RuntimeError("Development server could not start; check whether the port is already in use.")
                try:
                    if client.get("/healthz").status_code == 200:
                        break
                except httpx.TransportError:
                    pass
                if time.monotonic() >= deadline:
                    raise RuntimeError("Development server did not become ready.")
                time.sleep(0.05)
        print("Development mock SMS enabled. Codes auto-fill in the development desktop; no real SMS is sent.", flush=True)
        arguments = ["--smoke", "--smoke-auth"] if args.smoke else []
        result = subprocess.run([str(executable), *arguments], env=env)
        return result.returncode
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())

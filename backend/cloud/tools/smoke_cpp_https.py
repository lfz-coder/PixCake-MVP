"""Run the C++ cloud chain against the real backend over locally verified TLS.

Usage from backend/cloud: PYTHONPATH=. .venv/bin/python tools/smoke_cpp_https.py
  ../../build-cloud-space/tests/test_cloud_https_integration
Only temporary fixture accounts, keys and files are used. No keychain changes.
"""
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


def certificates(root):
    config = root / "certificate.cnf"
    config.write_text("""[req]
distinguished_name = name
prompt = no
[name]
CN = PixCake isolated HTTPS test
[ca]
basicConstraints = critical,CA:TRUE
keyUsage = critical,keyCertSign,cRLSign
subjectKeyIdentifier = hash
[server]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth
subjectAltName = IP:127.0.0.1
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
""", encoding="ascii")
    server_config = root / "server.cnf"
    server_config.write_text(config.read_text(encoding="ascii").replace("CN = PixCake isolated HTTPS test", "CN = 127.0.0.1"), encoding="ascii")
    commands = [
        ["req", "-new", "-x509", "-nodes", "-newkey", "rsa:2048", "-sha256", "-days", "1",
         "-config", str(config), "-extensions", "ca", "-keyout", str(root / "ca.key"), "-out", str(root / "ca.pem")],
        ["req", "-new", "-nodes", "-newkey", "rsa:2048", "-sha256", "-config", str(server_config),
         "-keyout", str(root / "server.key"), "-out", str(root / "server.csr")],
        ["x509", "-req", "-sha256", "-days", "1", "-in", str(root / "server.csr"), "-CA", str(root / "ca.pem"),
         "-CAkey", str(root / "ca.key"), "-CAcreateserial", "-extfile", str(config), "-extensions", "server", "-out", str(root / "server.pem")],
    ]
    for command in commands:
        subprocess.run(["/usr/bin/openssl", *command], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=15)
    for name in ("ca.key", "server.key"):
        (root / name).chmod(0o600)


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit("Supply test_cloud_https_integration and optionally the development shell executable.")
    executable = Path(sys.argv[1]).resolve(strict=True)
    shell = Path(sys.argv[2]).resolve(strict=True) if len(sys.argv) == 3 else None
    with tempfile.TemporaryDirectory(prefix="pixcake-cloud-https-") as directory:
        root = Path(directory)
        certificates(root)
        database = root / "cloud.sqlite"
        store = Store(database)
        store.initialize()
        alice, bob = store.create_account("TLS Alice"), store.create_account("TLS Bob")
        team = store.create_team(alice["account_id"], "HTTPS 团队空间")
        projects = [store.create_project(team, f"项目 {i}") for i in range(3)]
        manifest = root / "fixture.token"
        alice_token = store.issue_session(alice["account_id"])
        with open(manifest, "x", encoding="ascii", opener=lambda path, flags: os.open(path, flags, 0o600)) as output:
            output.write("\n".join([alice["account_id"], alice_token,
                                    bob["account_id"], store.issue_session(bob["account_id"]), team,
                                    alice["personal_space_id"], bob["personal_space_id"], *projects]) + "\n")
        private_token = root / "development.token"
        with open(private_token, "x", encoding="ascii", opener=lambda path, flags: os.open(path, flags, 0o600)) as output:
            output.write(alice_token + "\n")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        origin = f"https://127.0.0.1:{port}"
        env = {**os.environ, "PIXCAKE_CLOUD_DB": str(database), "PIXCAKE_CLOUD_CURSOR_SECRET": secrets.token_urlsafe(32)}
        process = subprocess.Popen([
            sys.executable, "-m", "uvicorn", "tools.https_fixture:create_fixture_app", "--factory", "--host", "127.0.0.1",
            "--port", str(port), "--ssl-certfile", str(root / "server.pem"), "--ssl-keyfile", str(root / "server.key"),
            "--http", "h11", "--loop", "asyncio", "--ws", "none", "--no-proxy-headers", "--no-access-log", "--no-server-header",
            "--limit-concurrency", "64", "--timeout-keep-alive", "5", "--h11-max-incomplete-event-size", "32768",
        ], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            context = ssl.create_default_context(cafile=str(root / "ca.pem"))
            with httpx.Client(base_url=origin, verify=context, trust_env=False, timeout=2) as client:
                deadline = time.monotonic() + 10
                last_readiness = "No health response"
                while True:
                    if process.poll() is not None:
                        raise RuntimeError("TLS fixture server exited before readiness.")
                    try:
                        status = client.get("/healthz").status_code
                        if status == 200:
                            break
                        last_readiness = f"Health HTTP status {status}"
                    except httpx.TransportError as error:
                        # Only this unauthenticated fixture health request is
                        # diagnosed; no token or application response is logged.
                        last_readiness = str(error)
                    if time.monotonic() >= deadline:
                        raise RuntimeError(f"TLS fixture server did not become ready: {last_readiness}")
                    time.sleep(0.05)
            if shell:
                shell_env = {key: value for key, value in env.items() if not key.startswith("PIXCAKE_ASSISTANT_")}
                shell_env.update(QT_QPA_PLATFORM="offscreen", PIXCAKE_CLOUD_URL=origin,
                                 PIXCAKE_CLOUD_CA_FILE=str(root / "ca.pem"))
                subprocess.run([str(shell), "--dev-cloud-session-file", str(private_token), "--smoke"],
                               env=shell_env, check=True, timeout=30)
                linked_token = root / "linked.token"
                linked_token.symlink_to(private_token)
                broad_token = root / "broad.token"
                broad_token.write_text(alice_token, encoding="ascii")
                broad_token.chmod(0o644)
                oversized_token = root / "oversized.token"
                oversized_token.write_text(alice_token + "invalid", encoding="ascii")
                oversized_token.chmod(0o600)
                pipe_token = root / "pipe.token"
                os.mkfifo(pipe_token, 0o600)
                for rejected in (linked_token, broad_token, oversized_token, pipe_token):
                    result = subprocess.run([str(shell), "--dev-cloud-session-file", str(rejected), "--smoke"],
                                            env=shell_env, capture_output=True, text=True, timeout=15)
                    assert result.returncode == 9
                    assert alice_token not in result.stdout + result.stderr
                print("Desktop cloud page passed: verified restoration, spaces, project rows and UI rendering.")
            subprocess.run([str(executable), origin, str(root / "ca.pem"), str(manifest)], check=True, timeout=45)
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()

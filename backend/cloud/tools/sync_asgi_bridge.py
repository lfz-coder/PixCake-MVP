"""Test-only pipe transport to the real API. No listener, sockets or TLS.

Used to verify C++ file-sync business behavior when network integration cannot
run. It does not replace smoke_sync_https or prove HTTPS behavior.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fastapi.testclient import TestClient
from pixcake_cloud.api import create_app
from pixcake_cloud.config import Settings
from pixcake_cloud.store import Store


def main():
    with tempfile.TemporaryDirectory(prefix="pixcake-sync-asgi-") as directory:
        store = Store(Path(directory) / "cloud.sqlite")
        store.initialize()
        account = store.create_account("ASGI integration test")
        settings = Settings(store.database, "isolated-asgi-test-secret-at-least-32-bytes")
        with TestClient(create_app(settings)) as client:
            print(json.dumps({"token": store.issue_session(account["account_id"]), "space": account["personal_space_id"]}), flush=True)
            for line in sys.stdin:
                request = json.loads(line)
                response = client.request(request["method"], request["path"], headers=request["headers"],
                                          content=bytes(request["body"]), follow_redirects=False)
                print(json.dumps({"status": response.status_code, "headers": list(response.headers.multi_items()),
                                  "body": list(response.content)}, separators=(",", ":")), flush=True)


if __name__ == "__main__":
    main()

import asyncio
import json
import struct
import unittest
from unittest.mock import patch

from pixcake_agent.config import AgentConfig
from pixcake_agent.gateway import FixturePixCakeGateway, LocalUnixPixCakeGateway, request_context


class GatewayTests(unittest.IsolatedAsyncioTestCase):
    async def test_fixture_preview_and_revision_guard(self):
        gateway = FixturePixCakeGateway()
        context = await gateway.call("get_edit_context", {"image_id": "image-1"})
        self.assertTrue(context.ok)
        self.assertEqual(context.payload["parameter_descriptors"][0]["parameter_id"], "exposure")
        preview = await gateway.call("preview_edit_plan", {"plan_id": "plan-1"})
        self.assertTrue(preview.ok)
        self.assertTrue(preview.payload["preview_id"].startswith("preview-"))

        pending = await gateway.call("commit_edit_plan", {"plan_id": "plan-1", "base_revision": 0})
        committed = gateway.resolve_approval(pending.payload["approval_id"], True)
        self.assertTrue(committed.ok)
        stale_pending = await gateway.call("commit_edit_plan", {"plan_id": "plan-2", "base_revision": 0})
        stale = gateway.resolve_approval(stale_pending.payload["approval_id"], True)
        self.assertFalse(stale.ok)
        self.assertEqual(stale.code, "state_conflict")

    async def test_unix_gateway_uses_bounded_length_prefixed_json(self):
        seen: dict[str, object] = {}

        class FakeSocket:
            def __init__(self, *_args: object) -> None:
                encoded = json.dumps({"ok": True, "payload": {"revision": 4}}).encode("utf-8")
                self.incoming = memoryview(struct.pack("<I", len(encoded)) + encoded)

            def __enter__(self) -> "FakeSocket":
                return self

            def __exit__(self, *_args: object) -> None:
                return None

            def settimeout(self, _timeout: float) -> None:
                return None

            def connect(self, path: str) -> None:
                seen["path"] = path

            def sendall(self, data: bytes) -> None:
                size = struct.unpack("<I", data[:4])[0]
                seen.update(json.loads(data[4 : 4 + size].decode("utf-8")))

            def recv(self, size: int) -> bytes:
                chunk = self.incoming[:size].tobytes()
                self.incoming = self.incoming[len(chunk) :]
                return chunk

        with patch("pixcake_agent.gateway.socket.socket", FakeSocket):
            gateway = LocalUnixPixCakeGateway(AgentConfig(gateway_socket="/tmp/assistant.sock"))
            with request_context(
                {
                    "request_id": "request-1",
                    "thread_id": "thread-1",
                    "message": "调整曝光",
                    "base_revision": 3,
                }
            ):
                reply = await gateway.call("get_edit_context", {"image_id": "image-1"})
            self.assertTrue(reply.ok)
            self.assertEqual(reply.payload["revision"], 4)
            self.assertEqual(seen["command"], "get_edit_context")
            self.assertEqual(seen["request"]["base_revision"], 3)


if __name__ == "__main__":
    unittest.main()

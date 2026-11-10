import json
import unittest

from pixcake_agent.protocol import AssistantRequest, EditPlan, ProtocolError, to_json


class ProtocolTests(unittest.TestCase):
    def test_request_round_trip_keeps_references_only(self):
        request = AssistantRequest.from_dict(
            {
                "request_id": "req-1",
                "thread_id": "thread-1",
                "message": "肤色亮半档",
                "image": {"image_id": "image-1", "source_revision": 4},
                "selection": {"selection_id": "face-1", "kind": "face"},
                "base_revision": 7,
            }
        )
        encoded = json.loads(to_json(request))
        self.assertEqual(encoded["image"]["image_id"], "image-1")
        self.assertNotIn("pixels", encoded)

    def test_rejects_stale_or_malformed_identifiers(self):
        with self.assertRaises(ProtocolError):
            AssistantRequest.from_dict({"request_id": "", "thread_id": "t", "message": "x"})
        with self.assertRaises(ProtocolError):
            AssistantRequest.from_dict({"request_id": "r", "thread_id": "t", "message": "x", "base_revision": -1})
        with self.assertRaises(ProtocolError):
            AssistantRequest.from_dict({"request_id": "r", "thread_id": "t", "message": "x", "image": []})

    def test_plan_limits_operations(self):
        with self.assertRaises(ProtocolError):
            EditPlan.from_dict(
                {
                    "plan_id": "p",
                    "request_id": "r",
                    "summary": "too many",
                    "operations": [
                        {"operation_id": str(i), "tool_id": "adjust"} for i in range(65)
                    ],
                }
            )
        with self.assertRaises(ProtocolError):
            EditPlan.from_dict(
                {
                    "protocol_version": 2,
                    "plan_id": "p",
                    "request_id": "r",
                    "summary": "wrong version",
                    "operations": [{"operation_id": "o", "tool_id": "adjust"}],
                }
            )


if __name__ == "__main__":
    unittest.main()

import json
import unittest

from pixcake_agent.events import AssistantEvent, SseParser, encode_event


class EventTests(unittest.TestCase):
    def test_parser_handles_arbitrary_chunk_boundaries(self):
        raw = encode_event(AssistantEvent("plan.ready", json.dumps({"plan_id": "p"}), "e1"))
        parser = SseParser()
        events = []
        for index in range(0, len(raw), 3):
            events.extend(parser.feed(raw[index : index + 3]))
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0].event, "plan.ready")
        self.assertEqual(events[0].event_id, "e1")
        self.assertEqual(events[0].json(), {"plan_id": "p"})

    def test_multiline_data_is_joined(self):
        parser = SseParser()
        events = parser.feed(b"event: message\ndata: first\ndata: second\n\n")
        self.assertEqual(events[0].data, "first\nsecond")

    def test_finish_flushes_a_frame_without_a_trailing_blank_line(self):
        parser = SseParser()
        parser.feed("event: completed\ndata: {}\n")
        self.assertEqual(parser.finish()[0].event, "completed")


if __name__ == "__main__":
    unittest.main()

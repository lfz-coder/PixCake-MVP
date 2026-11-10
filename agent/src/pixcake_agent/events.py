"""Small SSE codec for the sidecar's JSON event stream.

The Qt client can feed arbitrary network chunks to :class:`SseParser`; it does
not need to assume that a chunk ends on an event boundary.  The parser does
not interpret model text or execute commands.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Any

MAX_EVENT_BYTES = 256 * 1024


@dataclass(frozen=True)
class AssistantEvent:
    event: str
    data: str
    event_id: str = ""

    def json(self) -> Any:
        return json.loads(self.data)


class SseParser:
    """Incrementally parse UTF-8 SSE frames separated by a blank line."""

    def __init__(self) -> None:
        self._buffer = ""
        self._event = "message"
        self._event_id = ""
        self._data: list[str] = []
        self._event_bytes = 0

    def feed(self, chunk: bytes | str) -> list[AssistantEvent]:
        if isinstance(chunk, bytes):
            text = chunk.decode("utf-8", errors="strict")
        else:
            text = chunk
        self._buffer += text
        events: list[AssistantEvent] = []
        while True:
            boundary = self._buffer.find("\n")
            if boundary < 0:
                break
            line = self._buffer[:boundary]
            self._buffer = self._buffer[boundary + 1 :]
            if line.endswith("\r"):
                line = line[:-1]
            self._event_bytes += len(line.encode("utf-8")) + 1
            if self._event_bytes > MAX_EVENT_BYTES:
                self._reset_event()
                raise ValueError("SSE event exceeds the PixCake size limit")
            if not line:
                event = self._finish_event()
                if event is not None:
                    events.append(event)
                continue
            if line.startswith(":"):
                continue
            field, separator, value = line.partition(":")
            if separator and value.startswith(" "):
                value = value[1:]
            if field == "event":
                self._event = value or "message"
            elif field == "id":
                self._event_id = value
            elif field == "data":
                self._data.append(value)
        return events

    def finish(self) -> list[AssistantEvent]:
        if self._buffer:
            events = self.feed("\n")
            event = self._finish_event()
            if event is not None:
                events.append(event)
            return events
        event = self._finish_event()
        return [event] if event is not None else []

    def _finish_event(self) -> AssistantEvent | None:
        if not self._data and self._event == "message" and not self._event_id:
            self._reset_event()
            return None
        event = AssistantEvent(self._event or "message", "\n".join(self._data), self._event_id)
        self._reset_event()
        return event

    def _reset_event(self) -> None:
        self._event = "message"
        self._event_id = ""
        self._data.clear()
        self._event_bytes = 0


def encode_event(event: AssistantEvent) -> bytes:
    """Encode one event for the LangGraph-compatible stream."""

    lines = [f"event: {event.event}"]
    if event.event_id:
        lines.append(f"id: {event.event_id}")
    lines.extend(f"data: {line}" for line in event.data.splitlines() or [""])
    return ("\n".join(lines) + "\n\n").encode("utf-8")
